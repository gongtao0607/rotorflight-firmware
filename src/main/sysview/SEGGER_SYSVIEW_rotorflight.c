#include "SEGGER_SYSVIEW.h"
#include <stddef.h>
#include "fc/tasks.h"

//
// Services provided to SYSVIEW by Rotorflight
//
static void SYSVIEW_SendTaskList(void)
{
    for (int i = 0; i < TASK_COUNT; i++) {
        task_t *task = getTask(i);

        SEGGER_SYSVIEW_TASKINFO Info;

        memset(&Info, 0, sizeof(Info));
        //
        // Fill elements with current task information
        //
        Info.TaskID = (uint32_t)task;
        if (task->attribute) {
            Info.sName = task->attribute->taskName;
            Info.Prio = task->attribute->staticPriority;
        }
        SEGGER_SYSVIEW_SendTaskInfo(&Info);
    }
}
const SEGGER_SYSVIEW_OS_API SYSVIEW_OS_API_ROTORFLIGHT = {
    NULL,
    SYSVIEW_SendTaskList,
};