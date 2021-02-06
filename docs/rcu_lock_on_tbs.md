# rcu read lock relocate
We remove rcu read lock from outer loop of `cpu_exec`, and place it somewhere more accurate, in the function of `tb_lookup__cpu_state`. The original place isn't suitable in the case of iqemu, because in some thread like a message loop, the `cpu_exec` almost never returns, leaving rcu read lock on forever. In turn `call_rcu` never gets a chance to execute. However, the only writer in the core of qemu is the qht writer, so we can effectively limit the rcu critical section to the only read operation that operates on the qht table not protected by `mmap_lock`. For more information, see the appendix table below.

# Use of `atomic_rcu_set()` and `atomic_rcu_read()`

They are nothing but `atomic_write_acquire` and `atomic_read_consume` operations. It is safe for them to stay out of the rcu critical section, though it breaks the coding format. Maybe a TODO.

# Appendixes

## qht table functions that call `call_rcu`
```md
qht_do_resize_reset
    qht_do_resize
        qht_grow_maybe
            qht_insert  // API
               tb_link_page
        qht_resize      // API. not used
    qht_do_resize_and_reset
        qht_reset_size  // API
            do_tb_flush
```

## All code that operates on `&tb_ctx.htable`
**Writes:**
```c
qht_reset_size
    do_tb_flush
        tb_flush
            tb_gen_code     // mmap_locked
qht_remove
    do_tb_phys_invalidate   // all ref routines mmap_locked
qht_insert
    tb_link_page
        tb_gen_code         // mmap_locked
```

**Reads:**
```c
qht_iter
    tb_invalidate_check     // rcu_read_lock not used in the first place, but mmap_locked.
    tb_page_check
        tb_link_page
            tb_gen_code     // mmap_locked
    qht_lookup_custom
        tb_htable_lookup
            tb_lookup__cpu_state    // IQEMU: Surround it by rcu_read_lock
```
