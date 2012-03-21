#include "state.h"

#include "log.h"

#include <pthread.h>

/* the current state. can be STATE_ONLINE, STATE_OFFLINE or STATE_EXITING */
static int state = STATE_OFFLINE;

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
