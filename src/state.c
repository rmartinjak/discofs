/* discofs - disconnected file system
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "state.h"

#include "log.h"
#include "worker.h"

#include <pthread.h>

/* the current state. can be STATE_ONLINE, STATE_OFFLINE or STATE_EXITING */
static int state = STATE_OFFLINE;
static int state_force_offline = 0;

/* mutex for getting/setting the state */
static pthread_mutex_t m_state = PTHREAD_MUTEX_INITIALIZER;

/* returns the current state */
int state_get()
{
    return state;
}

/* set the new state; this has no effect when current state is STATE_EXITING */
void state_set(int s, int *oldstate)
{
    /* put old state in caller-provided pointer */
    if (oldstate)
        *oldstate = state;

    /* don't change status when exiting */
    if (state == STATE_EXITING)
        return;

    pthread_mutex_lock(&m_state);

    /* log new state if it differs from old state */
    if (s == STATE_ONLINE && s != state)
        VERBOSE("changing state to ONLINE\n");
    else if (s == STATE_OFFLINE && s != state)
        VERBOSE("changing state to OFFLINE\n");
    else if (s == STATE_EXITING && s != state)
        VERBOSE("changing state to EXITING\n");

    state = s;

    pthread_mutex_unlock(&m_state);
}

/* turn "force offline" on/off */
void state_toggle_force_offline(void)
{
    state_force_offline = !state_force_offline;
    INFO("force offline: %s\n", state_force_offline ? "on" : "off");
}

/* state checking thread */
void *state_check_main(void *arg)
{
    int oldstate = STATE_OFFLINE;

    while (oldstate != STATE_EXITING)
    {
        sleep(SLEEP_SHORT);

        /* check and set state */
        if (
            !state_force_offline
            && is_running(discofs_options.pid_file)
            && is_reachable(discofs_options.host)
            && is_mounted(discofs_options.remote_root)
           )
         {

            state_set(STATE_ONLINE, &oldstate);

            if (oldstate == STATE_OFFLINE)
                worker_wakeup();
        }
        else
        {
            state_set(STATE_OFFLINE, &oldstate);
        }
    }

    return NULL;
}
