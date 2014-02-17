/* Orx - Portable Game Engine
 *
 * Copyright (c) 2008-2013 Orx-Project
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 *    1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 *
 *    2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 *
 *    3. This notice may not be removed or altered from any source
 *    distribution.
 */

/**
 * @file orxThread.c
 * @date 24/11/2013
 * @author iarwain@orx-project.org
 *
 */

#define NO_WIN32_LEAN_AND_MEAN

#include "core/orxThread.h"

#include "core/orxClock.h"
#include "core/orxSystem.h"
#include "debug/orxDebug.h"
#include "memory/orxMemory.h"
#include "utils/orxString.h"


#ifdef __orxWINDOWS__

  #include <process.h>

#else /* __orxWINDOWS__ */

  #include <pthread.h>
  #include <errno.h>
  #include <sys/types.h>
  #include <unistd.h>

  #if defined(__orxLINUX__) || defined(__orxRASPBERRY_PI__)

    #include <sched.h>

  #endif /* __orxLINUX__ || __orxRASPBERRY_PI__ */

#endif /* __orxWINDOWS__ */


#if defined(__orxX86_64__) || defined(__orxPPC64__)

  #define orxTHREAD_CAST_HELPER   (orxU64)

#else /* __orxX86_64__ || __orxPPC64__ */

  #define orxTHREAD_CAST_HELPER

#endif /* __orxX86_64__ || __orxPPC64__ */


/** Module flags
 */
#define orxTHREAD_KU32_STATIC_FLAG_NONE               0x00000000  /**< No flags have been set */
#define orxTHREAD_KU32_STATIC_FLAG_READY              0x00000001  /**< Static flag */
#define orxTHREAD_KU32_STATIC_FLAG_REGISTERED         0x80000000  /**< Registered flag */
#define orxTHREAD_KU32_STATIC_MASK_ALL                0xFFFFFFFF  /**< The module has been initialized */

#define orxTHREAD_KU32_INFO_FLAG_NONE                 0x00000000  /**< No flags have been set */
#define orxTHREAD_KU32_INFO_FLAG_INITIALIZED          0x00000001  /**< Initialized flag */
#define orxTHREAD_KU32_INFO_FLAG_STOP                 0x10000000  /**< Stop flag */
#define orxTHREAD_KU32_INFO_MASK_ALL                  0xFFFFFFFF  /**< The module has been initialized */


/** Misc
 */
#define orxTHREAD_KU32_TASK_LIST_SIZE                 64


/***************************************************************************
 * Structure declaration                                                   *
 ***************************************************************************/

/* Thread info structure
 */
typedef struct __orxTHREAD_INFO_t
{
#ifdef __orxWINDOWS__

  HANDLE                  hThread;
  orxU32                  u32ThreadID;

#else /* __orxWINDOWS__ */

  pthread_t               hThread;

#endif /* __orxWINDOWS__ */

  orxTHREAD_FUNCTION      pfnRun;
  void                   *pContext;
  orxU32                  u32ParentID;
  orxU32                  u32Flags;

} orxTHREAD_INFO;

/** Task
 */
typedef struct __orxTHREAD_TASK_t
{
  orxTHREAD_FUNCTION      pfnRun;
  orxTHREAD_FUNCTION      pfnThen;
  orxTHREAD_FUNCTION      pfnElse;
  void                   *pContext;
  orxSTATUS               eResult;

} orxTHREAD_TASK;

/** Static structure
 */
typedef struct __orxTHREAD_STATIC_t
{
  orxTHREAD_SEMAPHORE*    pstThreadSemaphore;
  orxTHREAD_SEMAPHORE*    pstTaskSemaphore;
  orxU32                  u32WorkerID;
  volatile orxU32         u32TaskInIndex;
  volatile orxU32         u32TaskProcessIndex;
  volatile orxU32         u32TaskOutIndex;
  volatile orxTHREAD_INFO astThreadInfoList[orxTHREAD_KU32_MAX_THREAD_NUMBER];
  volatile orxTHREAD_TASK astTaskList[orxTHREAD_KU32_TASK_LIST_SIZE];

  orxU32                  u32Flags;

} orxTHREAD_STATIC;


/***************************************************************************
 * Static variables                                                        *
 ***************************************************************************/

/** Static data
 */
static orxTHREAD_STATIC sstThread;


/***************************************************************************
 * Private functions                                                       *
 ***************************************************************************/

#ifdef __orxWINDOWS__
static unsigned int WINAPI orxThread_Execute(void *_pContext)
#else /* __orxWINDOWS__ */
static void *orxThread_Execute(void *_pContext)
#endif /* __orxWINDOWS__ */
{
  volatile orxTHREAD_INFO  *pstInfo;
  orxSTATUS                 eResult;

  /* Gets thread's info */
  pstInfo = &(sstThread.astThreadInfoList[(orxU32) orxTHREAD_CAST_HELPER _pContext]);

#ifdef __orxWINDOWS__

  /* Stores thread ID */
  pstInfo->u32ThreadID = GetCurrentThreadId();
  orxMEMORY_BARRIER();

#endif /* __orxWINDOWS__ */

  do
  {
    /* Runs thread function */
    eResult = pstInfo->pfnRun(pstInfo->pContext);

    /* Yields */
    orxThread_Yield();
  }
  while((eResult != orxSTATUS_FAILURE) && !orxFLAG_TEST(pstInfo->u32Flags, orxTHREAD_KU32_INFO_FLAG_STOP));

  /* Done! */
  return 0;
}

static void orxFASTCALL orxThread_NotifyTask(const orxCLOCK_INFO *_pstClockInfo, void *_pContext)
{
  /* While there are processed tasks */
  while(sstThread.u32TaskOutIndex != sstThread.u32TaskProcessIndex)
  {
    volatile orxTHREAD_TASK *pstTask;

    /* Gets Task */
    pstTask = &(sstThread.astTaskList[sstThread.u32TaskOutIndex]);

    /* Succeeded? */
    if(pstTask->eResult != orxSTATUS_FAILURE)
    {
      /* Has THEN callback? */
      if(pstTask->pfnThen != orxNULL)
      {
        /* Calls it */
        pstTask->pfnThen(pstTask->pContext);
      }
    }
    else
    {
      /* Has ELSE callback? */
      if(pstTask->pfnElse != orxNULL)
      {
        /* Calls it */
        pstTask->pfnElse(pstTask->pContext);
      }
    }

    /* Updates task out index */
    orxMEMORY_BARRIER();
    sstThread.u32TaskOutIndex = (sstThread.u32TaskOutIndex + 1) & (orxTHREAD_KU32_TASK_LIST_SIZE - 1);
  }

  /* Done! */
  return;
}

static orxSTATUS orxFASTCALL orxThread_Work(void *_pContext)
{
  orxSTATUS eResult = orxSTATUS_SUCCESS;

  /* While there are pending requests */
  while(sstThread.u32TaskProcessIndex != sstThread.u32TaskInIndex)
  {
    volatile orxTHREAD_TASK *pstTask;

    /* Gets task */
    pstTask = &(sstThread.astTaskList[sstThread.u32TaskProcessIndex]);

    /* Runs it */
    pstTask->eResult = (pstTask->pfnRun != orxNULL) ? pstTask->pfnRun(pstTask->pContext) : orxSTATUS_SUCCESS;

    /* Updates task process index */
    orxMEMORY_BARRIER();
    sstThread.u32TaskProcessIndex = (sstThread.u32TaskProcessIndex + 1) & (orxTHREAD_KU32_TASK_LIST_SIZE - 1);
  }

  /* Done! */
  return eResult;
}


/***************************************************************************
 * Public functions                                                        *
 ***************************************************************************/

/** Thread module setup
 */
void orxFASTCALL orxThread_Setup()
{
  /* Adds module dependencies */
  orxModule_AddDependency(orxMODULE_ID_THREAD, orxMODULE_ID_MEMORY);
  orxModule_AddDependency(orxMODULE_ID_THREAD, orxMODULE_ID_SYSTEM);

  /* Done! */
  return;
}

/** Init the thread module
 * @return Returns the status of the operation
 */
orxSTATUS orxFASTCALL orxThread_Init()
{
  orxSTATUS eResult = orxSTATUS_FAILURE;

  /* Checks */
  orxASSERT(orxMath_IsPowerOfTwo(orxTHREAD_KU32_TASK_LIST_SIZE) != orxFALSE);

  /* Was not already initialized? */
  if(!(sstThread.u32Flags & orxTHREAD_KU32_STATIC_FLAG_READY))
  {
    /* Cleans static controller */
    orxMemory_Zero(&sstThread, sizeof(orxTHREAD_STATIC));

    /* Updates status */
    sstThread.u32Flags |= orxTHREAD_KU32_STATIC_FLAG_READY;

    /* Creates semaphores */
    sstThread.pstThreadSemaphore  = orxThread_CreateSemaphore(1);
    sstThread.pstTaskSemaphore    = orxThread_CreateSemaphore(1);

    /* Success? */
    if((sstThread.pstThreadSemaphore != orxNULL) && (sstThread.pstTaskSemaphore != orxNULL))
    {
#if defined(__orxWINDOWS__)

      /* Inits main thread info */
      sstThread.astThreadInfoList[orxTHREAD_KU32_MAIN_THREAD_ID].hThread      = GetCurrentThread();
      sstThread.astThreadInfoList[orxTHREAD_KU32_MAIN_THREAD_ID].u32ThreadID  = GetCurrentThreadId();
      sstThread.astThreadInfoList[orxTHREAD_KU32_MAIN_THREAD_ID].u32Flags     = orxTHREAD_KU32_INFO_FLAG_INITIALIZED;

      /* Sets thread CPU affinity to remain on the same core */
      SetThreadAffinityMask(sstThread.astThreadInfoList[orxTHREAD_KU32_MAIN_THREAD_ID].hThread, 1);

      /* Asks for small time slices */
      timeBeginPeriod(1);

#else /* __orxWINDOWS__ */

      /* Inits main thread info */
      sstThread.astThreadInfoList[orxTHREAD_KU32_MAIN_THREAD_ID].hThread  = pthread_self();
      sstThread.astThreadInfoList[orxTHREAD_KU32_MAIN_THREAD_ID].u32Flags = orxTHREAD_KU32_INFO_FLAG_INITIALIZED;

  #if defined(__orxLINUX__) || defined(__orxRASPBERRY_PI__)

      {
        cpu_set_t stSet;

        /* Sets CPU affinity mask */
        CPU_ZERO(&stSet);
        CPU_SET(0, &stSet);

        /* Applies it */
        pthread_setaffinity_np(sstThread.astThreadInfoList[orxTHREAD_KU32_MAIN_THREAD_ID].hThread, sizeof(cpu_set_t), &stSet);
      }

  #endif /* __orxLINUX__ || __orxRASPBERRY_PI__ */

#endif /* __orxWINDOWS__ */

      /* Creates worker thread */
      sstThread.u32WorkerID = orxThread_Start(&orxThread_Work, orxNULL);

      /* Success? */
      if(sstThread.u32WorkerID != orxU32_UNDEFINED)
      {
        /* Updates result */
        eResult = orxSTATUS_SUCCESS;
      }
      else
      {
        /* Logs message */
        orxDEBUG_PRINT(orxDEBUG_LEVEL_SYSTEM, "Couldn't start internal worker thread.");

        /* Deletes semaphores */
        orxThread_DeleteSemaphore(sstThread.pstThreadSemaphore);
        orxThread_DeleteSemaphore(sstThread.pstTaskSemaphore);

#ifdef __orxWINDOWS__

        /* Asks for small time slices */
        timeEndPeriod(1);

#endif /* __orxWINDOWS__ */

        /* Updates status */
        sstThread.u32Flags &= ~orxTHREAD_KU32_STATIC_FLAG_READY;
      }
    }
    else
    {
      /* Logs message */
      orxDEBUG_PRINT(orxDEBUG_LEVEL_SYSTEM, "Couldn't initialize internal semaphore.");

      /* Deletes semaphores */
      if(sstThread.pstThreadSemaphore != orxNULL)
      {
        orxThread_DeleteSemaphore(sstThread.pstThreadSemaphore);
      }
      if(sstThread.pstTaskSemaphore != orxNULL)
      {
        orxThread_DeleteSemaphore(sstThread.pstTaskSemaphore);
      }

      /* Updates status */
      sstThread.u32Flags &= ~orxTHREAD_KU32_STATIC_FLAG_READY;
    }
  }

  /* Done! */
  return eResult;
}

/** Exit the thread module
 */
void orxFASTCALL orxThread_Exit()
{
  /* Checks */
  if((sstThread.u32Flags & orxTHREAD_KU32_STATIC_FLAG_READY) == orxTHREAD_KU32_STATIC_FLAG_READY)
  {
    /* Joins all remaining threads */
    orxThread_JoinAll();

#ifdef __orxWINDOWS__

    /* Resets time slices */
    timeEndPeriod(1);

#endif /* __orxWINDOWS__ */

    /* Deletes semaphores */
    orxThread_DeleteSemaphore(sstThread.pstThreadSemaphore);
    orxThread_DeleteSemaphore(sstThread.pstTaskSemaphore);

    /* Cleans static controller */
    orxMemory_Zero(&sstThread, sizeof(orxTHREAD_STATIC));
  }

  /* Done! */
  return;
}

/** Starts a new thread
 * @param[in]   _pfnRun                               Function to run on the new thread
 * @param[in]   _pContext                             Context that will be transmitted to the function when called
 * @return      Thread ID if successful, orxU32_UNDEFINED otherwise
 */
orxU32 orxFASTCALL orxThread_Start(const orxTHREAD_FUNCTION _pfnRun, void *_pContext)
{
  orxU32 u32Index, u32Result = orxU32_UNDEFINED;

  /* Checks */
  orxASSERT((sstThread.u32Flags & orxTHREAD_KU32_STATIC_FLAG_READY) == orxTHREAD_KU32_STATIC_FLAG_READY);
  orxASSERT(_pfnRun != orxNULL);

  /* Waits for semaphore */
  orxThread_WaitSemaphore(sstThread.pstThreadSemaphore);

  /* For all slots */
  for(u32Index = 0; u32Index < orxTHREAD_KU32_MAX_THREAD_NUMBER; u32Index++)
  {
    /* Is unused? */
    if(!orxFLAG_TEST(sstThread.astThreadInfoList[u32Index].u32Flags, orxTHREAD_KU32_INFO_FLAG_INITIALIZED))
    {
      /* Selects it */
      break;
    }
  }

  /* Found? */
  if(u32Index != orxTHREAD_KU32_MAX_THREAD_NUMBER)
  {
    volatile orxTHREAD_INFO *pstInfo;

    /* Gets its info */
    pstInfo = &(sstThread.astThreadInfoList[u32Index]);

    /* Inits its info */
    pstInfo->pfnRun       = _pfnRun;
    pstInfo->pContext     = _pContext;
    pstInfo->u32ParentID  = orxThread_GetCurrent();
    pstInfo->u32Flags     = orxTHREAD_KU32_INFO_FLAG_INITIALIZED;
    orxMEMORY_BARRIER();

#ifdef __orxWINDOWS__

    /* Creates thread */
    pstInfo->hThread = (HANDLE)_beginthreadex(NULL, 0, orxThread_Execute, (void *)u32Index, 0, NULL);

    /* Success? */
    if(pstInfo->hThread != NULL)
    {
      /* Updates result */
      u32Result = u32Index;
    }
    else

#else /* __orxWINDOWS__ */

    /* Creates thread */
    if(pthread_create((pthread_t *)&(pstInfo->hThread), NULL, orxThread_Execute, (void *) orxTHREAD_CAST_HELPER u32Index) == 0)
    {
      /* Updates result */
      u32Result = u32Index;
    }
    else

#endif /* __orxWINDOWS__ */

    {
      /* Clears its info */
      pstInfo->hThread      = 0;
      pstInfo->pfnRun       = orxNULL;
      pstInfo->pContext     = orxNULL;
      pstInfo->u32ParentID  = 0;
      pstInfo->u32Flags     = orxTHREAD_KU32_INFO_FLAG_NONE;
    }
  }

  /* Signals semaphore */
  orxThread_SignalSemaphore(sstThread.pstThreadSemaphore);

  /* Done! */
  return u32Result;
}

/** Joins a thread (blocks & waits until the other thread finishes)
 * @param[in]   _u32ThreadID                          ID of the thread for which to wait
 * @return      orxSTATUS_SUCCESS / orxSTATUS_FAILURE
 */
orxSTATUS orxFASTCALL orxThread_Join(orxU32 _u32ThreadID)
{
  orxSTATUS eResult = orxSTATUS_FAILURE;

  /* Checks */
  orxASSERT((sstThread.u32Flags & orxTHREAD_KU32_STATIC_FLAG_READY) == orxTHREAD_KU32_STATIC_FLAG_READY);
  orxASSERT(_u32ThreadID < orxTHREAD_KU32_MAX_THREAD_NUMBER);

  /* Is initialized? */
  if(orxFLAG_TEST(sstThread.astThreadInfoList[_u32ThreadID].u32Flags, orxTHREAD_KU32_INFO_FLAG_INITIALIZED))
  {
    /* Waits for semaphore */
    orxThread_WaitSemaphore(sstThread.pstThreadSemaphore);

    /* Updates stop flag */
    orxFLAG_SET(sstThread.astThreadInfoList[_u32ThreadID].u32Flags, orxTHREAD_KU32_INFO_FLAG_STOP, orxTHREAD_KU32_INFO_FLAG_NONE);

#ifdef __orxWINDOWS__

    /* Waits for thread */
    WaitForSingleObject(sstThread.astThreadInfoList[_u32ThreadID].hThread, INFINITE);

    /* Clears thread ID */
    sstThread.astThreadInfoList[_u32ThreadID].u32ThreadID = 0;

#else /* __orxWINDOWS__ */

    /* Joins thread */
    pthread_join(sstThread.astThreadInfoList[_u32ThreadID].hThread, NULL);

#endif /* __orxWINDOWS__ */

    /* Clears thread info */
    sstThread.astThreadInfoList[_u32ThreadID].hThread     = 0;
    sstThread.astThreadInfoList[_u32ThreadID].u32ParentID = 0;
    sstThread.astThreadInfoList[_u32ThreadID].pfnRun      = orxNULL;
    sstThread.astThreadInfoList[_u32ThreadID].pContext    = orxNULL;
    sstThread.astThreadInfoList[_u32ThreadID].u32Flags    = orxTHREAD_KU32_INFO_FLAG_NONE;
    orxMEMORY_BARRIER();

    /* Signals semaphore */
    orxThread_SignalSemaphore(sstThread.pstThreadSemaphore);

    /* Updates result */
    eResult = orxSTATUS_SUCCESS;
  }

  /* Done! */
  return eResult;
}

/** Joins all threads (blocks & waits until the other threads finish)
 * @return      orxSTATUS_SUCCESS / orxSTATUS_FAILURE
 */
orxSTATUS orxFASTCALL orxThread_JoinAll()
{
  orxU32    u32Index;
  orxSTATUS eResult = orxSTATUS_SUCCESS;

  /* Checks */
  orxASSERT((sstThread.u32Flags & orxTHREAD_KU32_STATIC_FLAG_READY) == orxTHREAD_KU32_STATIC_FLAG_READY);

  /* For all slots */
  for(u32Index = 0; u32Index < orxTHREAD_KU32_MAX_THREAD_NUMBER; u32Index++)
  {
    /* Not main? */
    if(u32Index != orxTHREAD_KU32_MAIN_THREAD_ID)
    {
      /* Joins it */
      orxThread_Join(u32Index);
    }
  }

  /* Done! */
  return eResult;
}

/** Gets current thread ID
 * @return      Current thread ID
 */
orxU32 orxFASTCALL orxThread_GetCurrent()
{
  orxU32 u32Result = orxU32_UNDEFINED, i;

  /* Checks */
  orxASSERT((sstThread.u32Flags & orxTHREAD_KU32_STATIC_FLAG_READY) == orxTHREAD_KU32_STATIC_FLAG_READY);

#ifdef __orxWINDOWS__

  orxU32 u32ThreadID;

  /* Gets current thread ID */
  u32ThreadID = GetCurrentThreadId();

  /* For all threads */
  for(i = 0; i < orxTHREAD_KU32_MAX_THREAD_NUMBER; i++)
  {
    /* Matches? */
    if(sstThread.astThreadInfoList[i].u32ThreadID == u32ThreadID)
    {
      /* Updates result */
      u32Result = i;
      break;
    }
  }

#else /* __orxWINDOWS__ */

  pthread_t hThread;

  /* Gets current thread */
  hThread = pthread_self();

  /* For all threads */
  for(i = 0; i < orxTHREAD_KU32_MAX_THREAD_NUMBER; i++)
  {
    /* Matches? */
    if(sstThread.astThreadInfoList[i].hThread == hThread)
    {
      /* Updates result */
      u32Result = i;
      break;
    }
  }

#endif /* __orxWINDOWS__ */

  /* Done! */
  return u32Result;
}

/** Yields to other threads
 */
void orxFASTCALL orxThread_Yield()
{
  /* Checks */
  orxASSERT((sstThread.u32Flags & orxTHREAD_KU32_STATIC_FLAG_READY) == orxTHREAD_KU32_STATIC_FLAG_READY);

#ifdef __orxWINDOWS__

  /* Yields */
  Sleep(0);

#else /* __orxWINDOWS__ */

  /* Yields */
  sched_yield();

#endif /* __orxWINDOWS__ */

  /* Done! */
  return;
}

/** Creates a semaphore with s given value
 * @param[in]   _u32Value                             Value with which to init the semaphore
 * @return      orxTHREAD_SEMAPHORE / orxNULL
 */
orxTHREAD_SEMAPHORE *orxFASTCALL orxThread_CreateSemaphore(orxU32 _u32Value)
{
  orxTHREAD_SEMAPHORE *pstResult;

  /* Checks */
  orxASSERT((sstThread.u32Flags & orxTHREAD_KU32_STATIC_FLAG_READY) == orxTHREAD_KU32_STATIC_FLAG_READY);

#ifdef __orxWINDOWS__

  /* Creates semaphore */
  pstResult = (orxTHREAD_SEMAPHORE *)CreateSemaphore(NULL, (LONG)_u32Value, (LONG)_u32Value, NULL);

#else /* __orxWINDOWS__ */

  #ifdef __orxMAC__

  {
    orxCHAR acBuffer[256];

    /* Prints name */
    acBuffer[orxString_NPrint(acBuffer, 255, "orx_semaphore_%u_%u", (orxU32)getpid(), (orxU32)orxSystem_GetRealTime())] = orxCHAR_NULL;

    /* Opens semaphore */
    pstResult = (orxTHREAD_SEMAPHORE *)sem_open(acBuffer, O_CREAT, 0644, _u32Value);

    /* Success? */
    if((sem_t *)pstResult != SEM_FAILED)
    {
      /* Unlinks it */
      sem_unlink(acBuffer);
    }
    else
    {
      /* Updates result */
      pstResult = orxNULL;
    }
  }

  #else /* __orxMAC__ */

  /* Allocates semaphore */
  pstResult = (orxTHREAD_SEMAPHORE *)orxMemory_Allocate(sizeof(sem_t), orxMEMORY_TYPE_SYSTEM);

  /* Success? */
  if(pstResult != orxNULL)
  {
    /* Inits it */
    if(sem_init((sem_t *)pstResult, 0, _u32Value) == -1)
    {
      /* Frees it */
      orxMemory_Free(pstResult);

      /* Updates result */
      pstResult = orxNULL;
    }
  }

  #endif /* __orxMAC__ */

#endif /* __orxWINDOWS__ */

  /* Done! */
  return pstResult;
}

/** Deletes a semaphore
 * @param[in]   _pstSemaphore                         Concerned semaphore
 * @return      orxSTATUS_SUCCESS / orxSTATUS_FAILURE
 */
orxSTATUS orxFASTCALL orxThread_DeleteSemaphore(orxTHREAD_SEMAPHORE *_pstSemaphore)
{
  orxSTATUS eResult;

  /* Checks */
  orxASSERT((sstThread.u32Flags & orxTHREAD_KU32_STATIC_FLAG_READY) == orxTHREAD_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstSemaphore != orxNULL);

#ifdef __orxWINDOWS__

  /* Closes the semaphore */
  eResult = (CloseHandle((HANDLE)_pstSemaphore) != 0) ? orxSTATUS_SUCCESS : orxSTATUS_FAILURE;

#else /* __orxWINDOWS__ */

  #ifdef __orxMAC__

  /* Closes it */
  eResult = (sem_close((sem_t *)_pstSemaphore) == 0) ? orxSTATUS_SUCCESS : orxSTATUS_FAILURE;

  #else /* __orxMAC__ */

  /* Destroys semaphore */
  eResult = (sem_destroy((sem_t *)_pstSemaphore) != -1) ? orxSTATUS_SUCCESS : orxSTATUS_FAILURE;

  /* Frees it */
  orxMemory_Free(_pstSemaphore);

  #endif /* __orxMAC__ */

#endif /* __orxWINDOWS__ */

  /* Done! */
  return eResult;
}

/** Waits for a semaphore
 * @param[in]   _pstSemaphore                         Concerned semaphore
 * @return      orxSTATUS_SUCCESS / orxSTATUS_FAILURE
 */
orxSTATUS orxFASTCALL orxThread_WaitSemaphore(orxTHREAD_SEMAPHORE *_pstSemaphore)
{
  orxSTATUS eResult;

  /* Checks */
  orxASSERT((sstThread.u32Flags & orxTHREAD_KU32_STATIC_FLAG_READY) == orxTHREAD_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstSemaphore != orxNULL);

#ifdef __orxWINDOWS__

  /* Waits for semaphore */
  eResult = (WaitForSingleObject((HANDLE)_pstSemaphore, INFINITE) != WAIT_FAILED) ? orxSTATUS_SUCCESS : orxSTATUS_FAILURE;

#else /* __orxWINDOWS__ */

  {
    int iReturnCode;

    /* GDB-proof semaphore wait (when breaking in debug, wait can prematurally return with EINTR) */
    do
    {
      /* Waits */
      iReturnCode = sem_wait((sem_t *)_pstSemaphore);
    }
    while((iReturnCode == -1) && (errno == EINTR));

    /* Updates result */
    eResult = (iReturnCode != -1) ? orxSTATUS_SUCCESS : orxSTATUS_FAILURE;
  }

#endif /* __orxWINDOWS__ */

  /* Done! */
  return eResult;
}

/** Signals a semaphore
 * @param[in]   _pstSemaphore                         Concerned semaphore
 * @return      orxSTATUS_SUCCESS / orxSTATUS_FAILURE
 */
orxSTATUS orxFASTCALL orxThread_SignalSemaphore(orxTHREAD_SEMAPHORE *_pstSemaphore)
{
  orxSTATUS eResult;

  /* Checks */
  orxASSERT((sstThread.u32Flags & orxTHREAD_KU32_STATIC_FLAG_READY) == orxTHREAD_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstSemaphore != orxNULL);

#ifdef __orxWINDOWS__

  /* Releases semaphore */
  eResult = (ReleaseSemaphore((HANDLE)_pstSemaphore, 1, NULL) != 0) ? orxSTATUS_SUCCESS : orxSTATUS_FAILURE;

#else /* __orxWINDOWS__ */

  /* Posts to a semaphore */
  eResult = (sem_post((sem_t *)_pstSemaphore) != -1) ? orxSTATUS_SUCCESS : orxSTATUS_FAILURE;

#endif /* __orxWINDOWS__ */

  /* Done! */
  return eResult;
}

/** Runs an asynchronous task and optional follow-ups
 * @param[in]   _pfnRun                               Asynchronous task to run, executed on a different thread dedicated to tasks
 * @param[in]   _pfnThen                              Executed (on the main thread) if Run does *not* return orxSTATUS_FAILURE, can be orxNULL
 * @param[in]   _pfnElse                              Executed (on the main thread) if Run returns orxSTATUS_FAILURE, can be orxNULL
 * @param[in]   _pContext                             Context that will be transmitted to all the task functions
 * @return      orxSTATUS_SUCCESS / orxSTATUS_FAILURE
 */
orxSTATUS orxFASTCALL orxThread_RunTask(const orxTHREAD_FUNCTION _pfnRun, const orxTHREAD_FUNCTION _pfnThen, const orxTHREAD_FUNCTION _pfnElse, void *_pContext)
{
  orxSTATUS eResult;

  /* Checks */
  orxASSERT((sstThread.u32Flags & orxTHREAD_KU32_STATIC_FLAG_READY) == orxTHREAD_KU32_STATIC_FLAG_READY);

  /* Is notify callback not registered? */
  if(!orxFLAG_TEST(sstThread.u32Flags, orxTHREAD_KU32_STATIC_FLAG_REGISTERED))
  {
    /* Are we on main thread, is clock module initialized and did we register callback? */
    if((orxThread_GetCurrent() == orxTHREAD_KU32_MAIN_THREAD_ID)
    && (orxModule_IsInitialized(orxMODULE_ID_CLOCK) != orxFALSE)
    && (orxClock_Register(orxClock_FindFirst(orx2F(-1.0f), orxCLOCK_TYPE_CORE), orxThread_NotifyTask, orxNULL, orxMODULE_ID_RESOURCE, orxCLOCK_PRIORITY_LOWEST) != orxSTATUS_FAILURE))
    {
      /* Updates status */
      orxFLAG_SET(sstThread.u32Flags, orxTHREAD_KU32_STATIC_FLAG_REGISTERED, orxTHREAD_KU32_STATIC_FLAG_NONE);

      /* Updates result */
      eResult = orxSTATUS_SUCCESS;
    }
    else
    {
      /* Updates result */
      eResult = orxSTATUS_FAILURE;
    }
  }
  else
  {
    /* Updates result */
    eResult = orxSTATUS_SUCCESS;
  }

  /* Valid? */
  if(eResult != orxSTATUS_FAILURE)
  {
    volatile orxTHREAD_TASK  *pstTask;
    orxU32                    u32NextTaskIndex;

    /* Waits for semaphore */
    orxThread_WaitSemaphore(sstThread.pstTaskSemaphore);

    /* Gets next task index */
    u32NextTaskIndex = (sstThread.u32TaskInIndex + 1) & (orxTHREAD_KU32_TASK_LIST_SIZE - 1);

    /* Waits for a free slot */
    while(u32NextTaskIndex == sstThread.u32TaskOutIndex)
    {
      /* On main thread? */
      if(orxThread_GetCurrent() == orxTHREAD_KU32_MAIN_THREAD_ID)
      {
        /* Manually pumps some task notifications */
        orxThread_NotifyTask(orxNULL, orxNULL);
      }
    }
    /* Gets current task */
    pstTask = &(sstThread.astTaskList[sstThread.u32TaskInIndex]);

    /* Inits it */
    pstTask->pfnRun   = _pfnRun;
    pstTask->pfnThen  = _pfnThen;
    pstTask->pfnElse  = _pfnElse;
    pstTask->pContext = _pContext;

    /* Commits Task */
    orxMEMORY_BARRIER();
    sstThread.u32TaskInIndex = u32NextTaskIndex;

    /* Signals semaphore */
    orxThread_SignalSemaphore(sstThread.pstTaskSemaphore);
  }

  /* Done! */
  return eResult;
}
