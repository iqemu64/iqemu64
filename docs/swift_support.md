# Investigations on swift lang
The language of swift has its unique set of ABI standard, which is totaly not worthy of learning due to its constantly changing. Instead of writing a swift-unique ABI translation layer, we try to use a ARM64 version of swift stdlibs to completely overlook the complexities of the swift ABI.

## Swift on the github
Apple has an open source version of swift on the [github](https://github.com/apple/swift), but as always, it is not complete. 

>It can only be compiled against Xcode beta 1, not 2, 3, or 4, only 1. Written in 2020/8/7.

We can compile it from source but most of the stdlibs are missing. As the commit `da242bd2d4bfbbc1db1742ba58e66f1c168e6f22` says, the "obslete overlays" are removed, but he didn't tell us why. After a complete compilation of the swift, we found no trace of missing stdlibs. Of course we can always check out revisions before `da242b...` or even hard copy them to the revision we are working, but we didn't do that. Let's see why.

## Products that ship with swift
Apparently, any iOS apps that ship with swift, has a `Frameworks` folder that contains all the swift stdlibs. Apple does this for old systems that does not support swift yet. And in the binary of swift product, it uses `LC_LOAD_WEAK_DYLIB` to load stdlibs from `/usr/lib/swift/`, which makes dyld overlook the missing of swift stdlibs. In this case, dyld tries to solve symbols from stdlibs from `Frameworks`.

So technically we just need to delete all the stdlibs from `/usr/lib/swift/` to make the system work.

## Memo
>stdlibs that come with product are from `/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/swift-5.0/...`

> One of the differences between stdlibs from `Frameworks` and ones from `/usr/lib/swift/` is, they have different install names. One contains `@rpath` and the other begins with `/usr/lib/swift/`