#include "qemu/osdep.h"
#include <glib.h>
#include <malloc/malloc.h>

// Self malloc hooks
// Some apps(WeChat) uses malloc hooks. It leads to emulation
// of ARM function on top of another emulation. This is not
// allowed! Use our own version of malloc/frees to ensure
// no interruption is met during critical execution!

static malloc_zone_t *iqemu_malloc_zone;

static gpointer
iqemu_malloc(gsize n_bytes)
{
    return malloc_zone_malloc(iqemu_malloc_zone, n_bytes);
}

static gpointer
iqemu_realloc(gpointer mem,
                         gsize    n_bytes)
{
    return malloc_zone_realloc(iqemu_malloc_zone, mem, n_bytes);
}

static void
iqemu_free(gpointer mem)
{
    malloc_zone_free(iqemu_malloc_zone, mem);
}

void
hook_self_malloc()
{
    static GMemVTable vtable;
    memset(&vtable, 0, sizeof(vtable));
    
    iqemu_malloc_zone = malloc_create_zone(10000, 0);
    
    vtable.malloc = iqemu_malloc;
    vtable.realloc = iqemu_realloc;
    vtable.free = iqemu_free;
    
    g_mem_set_vtable(&vtable);
}
