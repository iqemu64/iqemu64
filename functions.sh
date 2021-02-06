
function fn_pre_check() {
    a=`whoami`
    if [[ "$a" == "root" ]]; then
        echo "try without sudo"
        exit 1
    fi

    xcodebuild -version

    a=`sw_vers | grep ProductVersion`
    if [[ ! "$a" =~ "10.15" ]]; then
        echo "For Catalina only"
        exit 1
    fi

    a=`csrutil status`
    if [[ ! "$a" =~ "disabled" ]]; then
        echo "We need SIP disabled"
        exit 1
    fi
}

function fn_variables() {
    read -p "Enter full path of Xcode.app(e.g., '/Users/username/Xcode.app') > " XcodePath
    readonly XcodePath

    if [ ! -e "$XcodePath" ]; then
        echo "No $XcodePath in your file system"
        exit 1
    fi

    user_Xcode_ver=`/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" ${XcodePath}/Contents/version.plist`
    echo "Xcode version: $user_Xcode_ver"
    if [[ "$user_Xcode_ver" != "11.4.1" ]]; then
        echo "We need Xcode 11.4.1"
        exit 1
    fi
    user_Xcode_ver="xcode${user_Xcode_ver}"
    readonly user_Xcode_ver
    # echo "${user_Xcode_ver}|"

    # ugly
    user_iOS_ver=`xcrun simctl list | grep com.apple.CoreSimulator.SimRuntime.iOS`
    user_iOS_ver=${user_iOS_ver#*\(}
    user_iOS_ver=${user_iOS_ver%% -*}
    user_iOS_ver="ios${user_iOS_ver}"
    readonly user_iOS_ver
    # echo "${user_iOS_ver}|"

    RuntimeRoot="${XcodePath}/Contents/Developer/Platforms/iPhoneOS.platform/Library/Developer/CoreSimulator/Profiles/Runtimes/iOS.simruntime/Contents/Resources/RuntimeRoot"
    SDKRoot="${XcodePath}/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator.sdk"
    readonly RuntimeRoot
    readonly SDKRoot

    OSX_name="Catalina"
    OSX_ver="osx10.15.5"
    iOS_ver="ios13.4.1"
    Xcode_ver="xcode11.4.1"
    readonly OSX_name
    readonly OSX_ver
    readonly iOS_ver
    readonly Xcode_ver

    iqemuPath=${PWD}
    readonly iqemuPath
}

function fn_symbol_links() {
    echo "\n==== dylibs and symbol links ===="
    dylib_folder="/usr/local/opt/gettext/lib"
    dylib="libintl.8.dylib"
    mkdir -p ${RuntimeRoot}/${dylib_folder}
    if [ ! -e ${RuntimeRoot}/${dylib_folder}/${dylib} ]; then
        cp ${dylib_folder}/${dylib} ${RuntimeRoot}/${dylib_folder}
    fi

    dylib_folder="/usr/local/opt/pcre/lib"
    dylib="libpcre.1.dylib"
    mkdir -p ${RuntimeRoot}/${dylib_folder}
    if [ ! -e ${RuntimeRoot}/${dylib_folder}/${dylib} ]; then
        cp ${dylib_folder}/${dylib} ${RuntimeRoot}/${dylib_folder}
    fi

    dylib_folder="/usr/local/opt/glib/lib"
    dylib="libglib-2.0.0.dylib"
    mkdir -p ${RuntimeRoot}/${dylib_folder}
    if [ ! -e ${RuntimeRoot}/${dylib_folder}/${dylib} ]; then
        cp ${dylib_folder}/${dylib} ${RuntimeRoot}/${dylib_folder}
    fi

    mkdir -p ${SDKRoot}/usr/local/lib/
    mkdir -p ${RuntimeRoot}/usr/local/lib/

    ln -fs ${RuntimeRoot}/usr/local/opt/glib/lib/libglib-2.0.0.dylib ${SDKRoot}/usr/local/lib/libglib-2.0.0.dylib
    ln -fs ${RuntimeRoot}/usr/local/opt/glib/lib/libglib-2.0.0.dylib ${SDKRoot}/usr/local/lib/libglib-2.0.dylib
}

function fn_iemukern() {
    if [ -e ${HOME}/Develop/kernels/iemukern.kext ]; then return; fi
    echo "\n==== Building iemukern ===="
    pushd iemukern
    # git switch ${OSX_name}
    mkdir -p ${HOME}/Develop/kernels/
    xcodebuild -target iemukern -scheme iemukern -derivedDataPath /tmp/iemukern -quiet -configuration Release
    popd

    echo "\n==== Loading iemukern ===="
    sudo cp -r ${HOME}/Develop/kernels/iemukern.kext /tmp/
    sudo kextutil -t /tmp/iemukern.kext
}

function fn_libunwind64() {
    echo "\n==== Building libunwind ===="
    pushd libunwind64
    ln -fs ${RuntimeRoot}/usr/lib/system ${SDKRoot}/usr/lib/system
    b="libunwind.dylib"
    a="${b}.backup-by-iqemu64"
    b_sim="libunwind_sim.dylib"
    if [ -e ${RuntimeRoot}/usr/lib/system/${a} ]; then popd; return; fi
    echo "backup ${RuntimeRoot}/usr/lib/system/${a}"
    cp ${RuntimeRoot}/usr/lib/system/${b} ${RuntimeRoot}/usr/lib/system/${a}

    rm ${RuntimeRoot}/usr/lib/system/${b_sim}
    xcodebuild -target libunwind_sim_install -scheme libunwind_sim_install -derivedDataPath /tmp/libunwind64 -destination "generic/platform=iOS Simulator" -quiet -configuration Release
    ln -fs ${RuntimeRoot}/usr/lib/system/${b_sim} ${RuntimeRoot}/usr/lib/system/${b}
    popd
}

function un_libunwind64() {
    pushd ${RuntimeRoot}/usr/lib/system/
    b="libunwind.dylib"
    a="${b}.backup-by-iqemu64"
    b_sim="libunwind_sim.dylib"
    if [ ! -e ${a} ]; then
        echo "${PWD}/${b} backup not found."
    else
        rm $b
        cp $a $b
        rm $a
        ln -fs $b $b_sim
    fi
    popd
}

function fn_libcxx() {
    if [ -e ${SDKRoot}/ARM64/usr/lib/libc++.1.dylib ]; then return; fi
    echo "\n==== Building libcxx ===="
    pushd libcxx-10.0.0
    mkdir -p ${SDKRoot}/ARM64/usr/lib/
    mkdir -p ${RuntimeRoot}/ARM64/usr/lib/
    xcodebuild -target ARM64_install -scheme ARM64_install -derivedDataPath /tmp/libcxx-10.0.0 -destination generic/platform=iOS -quiet -configuration Release
    ln -fs ${SDKRoot}/ARM64/usr/lib/libc++.1.dylib ${RuntimeRoot}/ARM64/usr/lib/libc++.1.dylib
    popd
}

function fn_dllmain_pswitch() {
    echo "\n==== Building dllmain and pswitch ===="
    pushd osxutils
    xcodebuild -target dllmain -scheme dllmain -derivedDataPath /tmp/dllmain -quiet -configuration Release
    pushd dllmain/pswitch
    xcodebuild -target pswitch -scheme pswitch -derivedDataPath /tmp/pswitch -quiet -configuration Release
    popd
    popd

    dylib_tmp="/usr/local/opt/gettext/lib/libintl.8.dylib"
    chmod u+w ${RuntimeRoot}/${dylib_tmp}
    pswitch -i ${RuntimeRoot}/${dylib_tmp} -v 0xd0400

    dylib_tmp="/usr/local/opt/glib/lib/libglib-2.0.0.dylib"
    chmod u+w ${RuntimeRoot}/${dylib_tmp}
    pswitch -i ${RuntimeRoot}/${dylib_tmp} -v 0xd0400

    dylib_tmp="/usr/local/opt/pcre/lib/libpcre.1.dylib"
    chmod u+w ${RuntimeRoot}/${dylib_tmp}
    pswitch -i ${RuntimeRoot}/${dylib_tmp} -v 0xd0400
}

function fn_HackedBinaries() {
    echo "\n==== HackedBinaries ===="
    pushd ${RuntimeRoot}/usr/lib
    b="dyld_sim"
    a="${b}.backup-by-iqemu64"
    if [ -e ${a} ]; then popd; return; fi
    echo "backup ${PWD}/${a}"
    cp $b $a
    sh ${iqemuPath}/deps/HackedBinaries/${iOS_ver}/${b}/patch.sh
    popd
}

function un_HackedBinaries() {
    pushd ${RuntimeRoot}/usr/lib
    b="dyld_sim"
    a="${b}.backup-by-iqemu64"
    if [ ! -e ${a} ]; then
        echo "${PWD}/${b} backup not found."
    else
        cp $a $b
        rm $a
    fi
    popd
}

function fn_iqemu64() {
    echo "\n==== Building iqemu64 ===="
    git stash save "tmp"
    git switch config_make
    ./configure --target-list=aarch64-bsd-user --disable-system --enable-bsd-user --disable-linux-user --enable-trace-backends=nop
    set +e
    make -s -k -j 4 # make will fail when linking
    set -e

    git stash save "tmp"
    git switch master
    xcodebuild -target iqemu64_install -scheme iqemu64_install -derivedDataPath /tmp/iqemu64 -quiet -configuration Release
    ln -fs ${SDKRoot}/usr/local/lib/libiqemu.dylib ${RuntimeRoot}/usr/local/lib/libiqemu.dylib

    # runtime deps data
    cp deps/all-functions.msgpack ${RuntimeRoot}/usr/local/lib/
}
