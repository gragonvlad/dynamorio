/* **********************************************************
 * Copyright (c) 2012 Google, Inc.  All rights reserved.
 * **********************************************************/

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * 
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * 
 * * Neither the name of VMware, Inc. nor the names of its contributors may be
 *   used to endorse or promote products derived from this software without
 *   specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL VMWARE, INC. OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#include "dr_api.h"

static byte *mybase;
static uint bb_execs;
static uint resurrect_success;
static bool verbose;

static
void at_bb(app_pc bb_addr)
{
#ifdef X64
    /* FIXME: abs ref is rip-rel so doesn't work w/o patching.
     * need to impl patching here: finalize interface.
     */
#else
    /* global reference => won't work w/o same base or relocation */
    bb_execs++;
#endif
}

/* We want to persist our clean call, which will only work if our library
 * is at the same base
 */

static size_t
event_persist_ro_size(void *drcontext, app_pc start, app_pc end, size_t file_offs,
                      void **user_data OUT)
{
    return sizeof(mybase);
}

static bool
event_persist_patch(void *drcontext, app_pc start, app_pc end,
                    byte *bb_start, byte *bb_end, void *user_data)
{
    /* XXX: add more sophisticated example that needs patching.
     * For that, we want cti's to be allowed, which is i#665.  Then
     * we can have a jmp or call into our lib (e.g., change at_bb to
     * not be inlinable) and go patch it to go through gencode or sthg,
     * and patch the gencode at load, or sthg.
     */
    return true;
}

static bool
event_persist_ro(void *drcontext, app_pc start, app_pc end, file_t fd, void *user_data)
{
    if (dr_write_file(fd, &mybase, sizeof(mybase)) != (ssize_t)sizeof(mybase))
        return false;
    return true;
}

static bool
event_resurrect_ro(void *drcontext, app_pc start, app_pc end, byte **map INOUT)
{
    byte *base = *(byte **)(*map);
    *map += sizeof(base);
    /* this test relies on having a preferred base and getting it both runs */
    if (base != mybase) {
        /* XXX: need to fix -persist_trust_textrel on linux (i#670)
         * (or add relocation support): currently only successfully
         * loading the executable's pcache
         */
        if (verbose) {
            dr_fprintf(STDERR, "persisted base="PFX" does not match cur base="PFX"\n",
                       base, mybase);
        }
        return false;
    }
    resurrect_success++;
    return true;
}

#define MINSERT instrlist_meta_preinsert

static dr_emit_flags_t
event_bb(void *drcontext, void *tag, instrlist_t *bb, bool for_trace, bool translating)
{
    dr_insert_clean_call(drcontext, bb, instrlist_first(bb), at_bb, false, 1,
                         OPND_CREATE_INTPTR((ptr_uint_t)dr_fragment_app_pc(tag)));
    return DR_EMIT_DEFAULT | DR_EMIT_PERSISTABLE;
}

static void
event_exit(void)
{
    if (resurrect_success > 0)
        dr_fprintf(STDERR, "successfully resurrected at least one pcache\n");
}

DR_EXPORT
void
dr_init(client_id_t id)
{
    mybase = dr_get_client_base(id);
    dr_fprintf(STDERR, "thank you for testing the client interface\n");
    dr_register_exit_event(event_exit);
    dr_register_bb_event(event_bb);
    if (!dr_register_persist_ro(event_persist_ro_size,
                                event_persist_ro,
                                event_resurrect_ro))
        dr_fprintf(STDERR, "failed to register ro");
    if (!dr_register_persist_patch(event_persist_patch))
        dr_fprintf(STDERR, "failed to register patch");
}