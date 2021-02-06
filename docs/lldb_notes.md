# LLDB is a piece of shit

LLDB has so many broken features that the developers don't even care for years. Documents on the offical website is also somehow outdated. In order to use it right, we need some notes on it.

## Extra wild signals or exceptions when ignoring signal stop

This is a long-standing problem that never changes throughout major versions of lldb. Once you type `proc hand -s false -p true SIGBUS`, the SIGBUS signal is ignored. However, the debug process is getting weirder and creeper after each legal stop of breakpoints or whatsoever, signals are getting messed up while none of these would happen if the program is not under debug. In order to get lldb work properly, we need some tricks, which leads to other problems of the fragile lldb.

## Xcode hangs when using python-based command stop-hook

A very strange problem, which doesn't make any sense. The stop-hook simply doesn't work in Xcode, while it does in terminal. The python script is used to auto-continue after a SIGBUS stop. As for the current version of Xcode 11.4(lldb-1103.0.22.8), stop-hook does not support python class handler, so the one-line handler of a python-based command is the only choice.

*REASON:* When used in Xcode with lldb-rpc-server, an IOHandlerThread is started inside lldb-rpc-server to receive inputs from debug terminal of Xcode, which is deadlocked when it engages a python-based command that waits for the same `stdin` in another thread. lldb terminal didn't hang because it does not have IOHandlerThread. Python-based breakpoint callback didn't hang because it set options explicitly not to set `stdin` for a python session. All we have to do is to binary patching in order to cancel setting `stdin` property when using a python-based command, but it also disables the ability of interactions of python-based commands. For now, I still don't know how to "interact" with a python-based command, so that side-effect is ignored.

## lldb behaves bizarrely when using synchronized mode pc-change operations like StepOut() etc. in python-based breakpoint callbacks

Just never use any pc-change operations in python-based breakpoint callbacks. It is NEVER documented, only mentioned in the source code. \
In Stopinfo.cpp:
>// FIXME: For now the callbacks have to run in async mode - the\
>// first time we restart we need\
>// to get out of there.  So set it here.\
>// When we figure out how to nest breakpoint hits then this will\
>// change.

Well, figure it out???

## lldb does not connect to a valid simulator device

    (lldb) platform select ios-simultor
    (lldb) platform connect <UUID>
    error: no device with UDID or name '<UUID>' was found

It also seems to be a long-standing problem which has lasted for at least 2 years or more. A bug ticket has been fired for this, but no one has ever cared.
The problem is in `PlatformiOSSimulatorCoreSimulatorSupport.mm, CoreSimulatorSupport::DeviceSet::GetAllDevices()`. When it tries to use `NSClassFromString(@"SimServiceContext")` while the host of objective-c class `SimServiceContext`, `/Library/Developer/PrivateFrameworks/CoreSimulator.framework/Versions/A/CoreSimulator` is never loaded in the process. Actually, `PlatformAppleSimulator::LoadCoreSimulator()` tried to do so, but with a wrong path. After manually loading the framework, the whole functionality is still problematic. We cannot use `target create` and `process launch` to launch a simulator app. lldb uses `-[SimDevice spawnWithPath:options:terminationQueue:terminationHandler:pid:error:]` to launch an application, which has been long abandoned by Xcode. Using it spawns the process, but no screen output in the simulator. Instead, Xcode uses `-[SimDevice launchApplicationAsyncWithID:options:completionQueue:completionHandler:]` with bundle ID to launch simulator apps. Options are set to wait_for_attach, then Xcode instructs lldb to attach to the process. lldb is outdated and left alone.

## stop-hook command cannot get the currently selected thread right.

Related function is `SBProcess.GetSelectedThread()`.

### Role duties when debugging with Xcode

Xcode launches the simulator app, and lldb-rpc-server.\
Xcode initiates every SB* debugger command, but they are LLDBRPC implmented instead of LLDB, which are later sent to lldb-rpc-server. LLDBRPC is closed sourced. Surprise.\
lldb-rpc-server does the actual actions implemented in LLDB. \
debugserver does all the dirty work.
