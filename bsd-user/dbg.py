import lldb

# Usage:
# (lldb) command script import /path/to/dbg.py
# (lldb) setbrk ${static_addr} ${binary}
# (lldb) setbrk ${dynamic_addr}

BRK_HELPER_FUNC = "dbg_remove_pending_bps"  # in dbg.c
OUT_ASM = "dbg_out"
lldb_target = None


def __lldb_init_module(debugger, internal_dict):
    cmd = "command script add -f dbg.BreakpointSet setbrk"
    debugger.HandleCommand(cmd)

    cmd = "breakpoint set -G true -n " + BRK_HELPER_FUNC
    debugger.HandleCommand(cmd)

    cmd = "breakpoint command add -F dbg.breakpoint_callback"
    debugger.HandleCommand(cmd)
    return


def breakpoint_callback(frame, bp_loc, dict):
    gprs = frame.regs.GetFirstValueByName("General Purpose Registers")

    out_asm = int(gprs.GetChildMemberWithName("rsi").GetValue(), 0)
    lldb_target.BreakpointCreateByAddress(out_asm)

    in_asm = int(gprs.GetChildMemberWithName("rdi").GetValue(), 0)
    print("\nbreak by arm pc", hex(in_asm), "at addr", hex(out_asm))

    # No need to stop here
    return False


def BreakpointSet(debugger, command, exe_ctx, result, internal_dict):
    target = exe_ctx.GetTarget()
    global lldb_target
    lldb_target = target

    args = command.split()
    addr = int(args[0], 0)
    offset = 0
    if (len(args) > 1):
        binary = args[1]
        file = lldb.SBFileSpec(binary)
        module = target.FindModule(file)
        if module:
            section = module.FindSection("__TEXT")
            offset = section.GetLoadAddress(target) - section.file_addr

    # print("offset:", hex(offset))
    addr += offset
    # print("addr with offset:", hex(addr))
    cmd = "p (void)dbg_get_inout_pair(" + str(addr) + ",&" + OUT_ASM + ")"
    debugger.HandleCommand(cmd)

    out_asm = target.FindFirstGlobalVariable(OUT_ASM).GetValue()
    if out_asm is None or int(out_asm, 0) == 0:
        debugger.HandleCommand("p (void)dbg_add_pending_bps(" + str(addr) + ")")
        print("Not translated. Pending...")
    else:
        debugger.HandleCommand("br s -a " + out_asm)
    return
