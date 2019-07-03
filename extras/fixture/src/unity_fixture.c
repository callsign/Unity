/* Copyright (c) 2010 James Grenning and Contributed to Unity Project
 * ==========================================
 *  Unity Project - A Test Framework for C
 *  Copyright (c) 2007 Mike Karlesky, Mark VanderVoord, Greg Williams
 *  [Released under MIT License. Please refer to license.txt for details]
 * ========================================== */

#ifdef UNITY_FIXTURE_TRACK_ALLOCATIONS
#include <pthread.h>
#endif

#include "unity_fixture.h"
#include "unity_internals.h"
#include <string.h>

#ifdef UNITY_FIXTURE_TRACK_ALLOCATIONS
#include <stdio.h>
#include <stdlib.h>
#include <execinfo.h>
#endif

struct UNITY_FIXTURE_T UnityFixture;

/* If you decide to use the function pointer approach.
 * Build with -D UNITY_OUTPUT_CHAR=outputChar and include <stdio.h>
 * int (*outputChar)(int) = putchar; */

#if !defined(UNITY_WEAK_ATTRIBUTE) && !defined(UNITY_WEAK_PRAGMA)
void setUp(void)    { /*does nothing*/ }
void tearDown(void) { /*does nothing*/ }
#endif

static void announceTestRun(unsigned int runNumber)
{
    UnityPrint("Unity test run ");
    UnityPrintNumberUnsigned(runNumber+1);
    UnityPrint(" of ");
    UnityPrintNumberUnsigned(UnityFixture.RepeatCount);
    UNITY_PRINT_EOL();
}

int UnityMain(int argc, const char* argv[], void (*runAllTests)(void))
{
    int result = UnityGetCommandLineOptions(argc, argv);
    unsigned int r;
    if (result != 0)
        return result;

    for (r = 0; r < UnityFixture.RepeatCount; r++)
    {
        UnityBegin(argv[0]);
        announceTestRun(r);
        runAllTests();
        if (!UnityFixture.Verbose) UNITY_PRINT_EOL();
        UnityEnd();
    }

    return (int)Unity.TestFailures;
}

static int selected(const char* filter, const char* name)
{
    if (filter == 0)
        return 1;
    return strstr(name, filter) ? 1 : 0;
}

static int testSelected(const char* test)
{
    return selected(UnityFixture.NameFilter, test);
}

static int groupSelected(const char* group)
{
    return selected(UnityFixture.GroupFilter, group);
}

void UnityTestRunner(unityfunction* setup,
                     unityfunction* testBody,
                     unityfunction* teardown,
                     const char* printableName,
                     const char* group,
                     const char* name,
                     const char* file,
                     unsigned int line)
{
    if (testSelected(name) && groupSelected(group))
    {
        Unity.TestFile = file;
        Unity.CurrentTestName = printableName;
        Unity.CurrentTestLineNumber = line;
        if (UnityFixture.Verbose)
        {
            UnityPrint(printableName);
        #ifndef UNITY_REPEAT_TEST_NAME
            Unity.CurrentTestName = NULL;
        #endif
        }
        else if (UnityFixture.Silent)
        {
            /* Do Nothing */
        }
        else
        {
            UNITY_OUTPUT_CHAR('.');
        }

        Unity.NumberOfTests++;
        UnityMalloc_StartTest();
        UnityPointer_Init();

        UNITY_EXEC_TIME_START();

        if (TEST_PROTECT())
        {
            setup();
            testBody();
        }
        if (TEST_PROTECT())
        {
            teardown();
        }
        if (TEST_PROTECT())
        {
            UnityPointer_UndoAllSets();
            if (!Unity.CurrentTestFailed)
                UnityMalloc_EndTest();
        }
        UnityConcludeFixtureTest();
    }
}

void UnityIgnoreTest(const char* printableName, const char* group, const char* name)
{
    if (testSelected(name) && groupSelected(group))
    {
        Unity.NumberOfTests++;
        Unity.TestIgnores++;
        if (UnityFixture.Verbose)
        {
            UnityPrint(printableName);
            UNITY_PRINT_EOL();
        }
        else if (UnityFixture.Silent)
        {
            /* Do Nothing */
        }
        else
        {
            UNITY_OUTPUT_CHAR('!');
        }
    }
}


/*------------------------------------------------- */
/* Malloc and free stuff */
#define MALLOC_DONT_FAIL -1
static int malloc_count;
static int malloc_fail_countdown = MALLOC_DONT_FAIL;

void UnityMalloc_StartTest(void)
{
    malloc_count = 0;
    malloc_fail_countdown = MALLOC_DONT_FAIL;
}

typedef struct GuardBytes
{
    size_t size;
    size_t guard_space;
#ifdef UNITY_FIXTURE_TRACK_ALLOCATIONS
    int frames;
    char **stack;
    const char *file;
    int line;
    struct GuardBytes *next;
    struct GuardBytes *prev;
#endif
} Guard;

#ifdef UNITY_FIXTURE_TRACK_ALLOCATIONS
static Guard *guard_first = NULL;
static Guard *guard_last = NULL;
#endif

void UnityMalloc_EndTest(void)
{
    malloc_fail_countdown = MALLOC_DONT_FAIL;
    if (malloc_count != 0)
    {
#ifdef UNITY_FIXTURE_TRACK_ALLOCATIONS
        fprintf(stderr, "%d mallocs not free()d\n", malloc_count);
        for (Guard *g = guard_first; g; g = g->next) {
            fprintf(stderr, "ALLOC(%zu from %s:%d)\n", g->size, g->file, g->line);
            for (int i = 0; i < g->frames; i++) {
                fprintf(stderr, "     %d - %s\n", i, g->stack[i]);
            }
        }
#endif
        UNITY_TEST_FAIL(Unity.CurrentTestLineNumber, "This test leaks!");
    }
}

void UnityMalloc_MakeMallocFailAfterCount(int countdown)
{
    malloc_fail_countdown = countdown;
}

/* These definitions are always included from unity_fixture_malloc_overrides.h */
/* We undef to use them or avoid conflict with <stdlib.h> per the C standard */
#undef malloc
#undef free
#undef calloc
#undef realloc

#ifdef UNITY_EXCLUDE_STDLIB_MALLOC
static unsigned char unity_heap[UNITY_INTERNAL_HEAP_SIZE_BYTES];
static size_t heap_index;
#else
#include <stdlib.h>
#endif


static const char end[] = "END";

#ifdef UNITY_FIXTURE_TRACK_ALLOCATIONS
void* unity_malloc(const char *file, int line, size_t size)
#else
void* unity_malloc(size_t size)
#endif
{
    char* mem;
    Guard* guard;
    size_t total_size = size + sizeof(Guard) + sizeof(end);
 
 #ifdef UNITY_FIXTURE_TRACK_ALLOCATIONS
   static pthread_t thread = 0;

    pthread_t self = pthread_self();
    if (thread) {
        if (thread != self) {
            fprintf(stderr, "DIFFERENT THREAD ALLOC %d != %d\n", (int)self, (int)thread);
            exit(1);
        }
    } else {
        thread = self;
    }
#endif

    if (malloc_fail_countdown != MALLOC_DONT_FAIL)
    {
        if (malloc_fail_countdown == 0)
            return NULL;
        malloc_fail_countdown--;
    }

    if (size == 0) return NULL;
#ifdef UNITY_EXCLUDE_STDLIB_MALLOC
    if (heap_index + total_size > UNITY_INTERNAL_HEAP_SIZE_BYTES)
    {
        guard = NULL;
    }
    else
    {
        guard = (Guard*)&unity_heap[heap_index];
        heap_index += total_size;
    }
#else
    guard = (Guard*)UNITY_FIXTURE_MALLOC(total_size);
#endif
    if (guard == NULL) return NULL;
    malloc_count++;
    guard->size = size;
    guard->guard_space = 0;

#ifdef UNITY_FIXTURE_TRACK_ALLOCATIONS
    if (guard_first) {
        guard_last->next = guard;
        guard->next = NULL;

        guard->prev = guard_last;
        guard_last = guard;
    } else {
        guard_first = guard_last = guard;
        guard->next = guard->prev = NULL;
    }
    guard->file = file;
    guard->line = line;

    void *bt[100];
    guard->frames = backtrace(bt, 100);
    guard->stack = backtrace_symbols(bt, guard->frames);
#endif

    mem = (char*)&(guard[1]);
    memcpy(&mem[size], end, sizeof(end));

    return (void*)mem;
}

static int isOverrun(void* mem)
{
    Guard* guard = (Guard*)mem;
    char* memAsChar = (char*)mem;
    guard--;

    return guard->guard_space != 0 || strcmp(&memAsChar[guard->size], end) != 0;
}

static void release_memory(void* mem)
{
    Guard* guard = (Guard*)mem;
    guard--;

    malloc_count--;

#ifdef UNITY_FIXTURE_TRACK_ALLOCATIONS
    Guard *next = guard->next;
    Guard *prev = guard->prev;

    if (prev) {
        prev->next = next;
    } else {
        guard_first = next;
    }

    if (next) {
        next->prev = prev;
    } else {
        guard_last = prev;
    }

    free(guard->stack);
    guard->stack = NULL;
#endif

#ifdef UNITY_EXCLUDE_STDLIB_MALLOC
    if (mem == unity_heap + heap_index - guard->size - sizeof(end))
    {
        heap_index -= (guard->size + sizeof(Guard) + sizeof(end));
    }
#else
    UNITY_FIXTURE_FREE(guard);
#endif
}

void unity_free(void* mem)
{
    int overrun;

    if (mem == NULL)
    {
        return;
    }

    overrun = isOverrun(mem);
    release_memory(mem);
    if (overrun)
    {
        UNITY_TEST_FAIL(Unity.CurrentTestLineNumber, "Buffer overrun detected during free()");
    }
}

#ifdef UNITY_FIXTURE_TRACK_ALLOCATIONS
void* unity_calloc(const char *file, int line, size_t num, size_t size)
#else
void* unity_calloc(size_t num, size_t size)
#endif
{
    void* mem = unity_malloc(file, line, num * size);
    if (mem == NULL) return NULL;
    memset(mem, 0, num * size);
    return mem;
}

#ifdef UNITY_FIXTURE_TRACK_ALLOCATIONS
void* unity_realloc(const char *file, int line, void* oldMem, size_t size)
#else
void* unity_realloc(void* oldMem, size_t size)
#endif
{
    Guard* guard = (Guard*)oldMem;
    void* newMem;

#ifdef UNITY_FIXTURE_TRACK_ALLOCATIONS
    if (oldMem == NULL) return unity_malloc(file, line, size);
#else
    if (oldMem == NULL) return unity_malloc(size);
#endif

    guard--;
    if (isOverrun(oldMem))
    {
        release_memory(oldMem);
        UNITY_TEST_FAIL(Unity.CurrentTestLineNumber, "Buffer overrun detected during realloc()");
    }

    if (size == 0)
    {
        release_memory(oldMem);
        return NULL;
    }

    if (guard->size >= size) return oldMem;

#ifdef UNITY_EXCLUDE_STDLIB_MALLOC /* Optimization if memory is expandable */
    if (oldMem == unity_heap + heap_index - guard->size - sizeof(end) &&
        heap_index + size - guard->size <= UNITY_INTERNAL_HEAP_SIZE_BYTES)
    {
        release_memory(oldMem);    /* Not thread-safe, like unity_heap generally */
#ifdef UNITY_FIXTURE_TRACK_ALLOCATIONS
        return unity_malloc(file, line, size); /* No memcpy since data is in place */
#else
        return unity_malloc(size); /* No memcpy since data is in place */
#endif
    }
#endif
#ifdef UNITY_FIXTURE_TRACK_ALLOCATIONS
    newMem = unity_malloc(file, line, size);
#else
    newMem = unity_malloc(size);
#endif

    if (newMem == NULL) return NULL; /* Do not release old memory */
    memcpy(newMem, oldMem, guard->size);
    release_memory(oldMem);
    return newMem;
}


/*-------------------------------------------------------- */
/*Automatic pointer restoration functions */
struct PointerPair
{
    void** pointer;
    void* old_value;
};

static struct PointerPair pointer_store[UNITY_MAX_POINTERS];
static int pointer_index = 0;

void UnityPointer_Init(void)
{
    pointer_index = 0;
}

void UnityPointer_Set(void** pointer, void* newValue, UNITY_LINE_TYPE line)
{
    if (pointer_index >= UNITY_MAX_POINTERS)
    {
        UNITY_TEST_FAIL(line, "Too many pointers set");
    }
    else
    {
        pointer_store[pointer_index].pointer = pointer;
        pointer_store[pointer_index].old_value = *pointer;
        *pointer = newValue;
        pointer_index++;
    }
}

void UnityPointer_UndoAllSets(void)
{
    while (pointer_index > 0)
    {
        pointer_index--;
        *(pointer_store[pointer_index].pointer) =
            pointer_store[pointer_index].old_value;
    }
}

int UnityGetCommandLineOptions(int argc, const char* argv[])
{
    int i;
    UnityFixture.Verbose = 0;
    UnityFixture.Silent = 0;
    UnityFixture.GroupFilter = 0;
    UnityFixture.NameFilter = 0;
    UnityFixture.RepeatCount = 1;

    if (argc == 1)
        return 0;

    for (i = 1; i < argc; )
    {
        if (strcmp(argv[i], "-v") == 0)
        {
            UnityFixture.Verbose = 1;
            i++;
        }
        else if (strcmp(argv[i], "-s") == 0)
        {
            UnityFixture.Silent = 1;
            i++;
        }
        else if (strcmp(argv[i], "-g") == 0)
        {
            i++;
            if (i >= argc)
                return 1;
            UnityFixture.GroupFilter = argv[i];
            i++;
        }
        else if (strcmp(argv[i], "-n") == 0)
        {
            i++;
            if (i >= argc)
                return 1;
            UnityFixture.NameFilter = argv[i];
            i++;
        }
        else if (strcmp(argv[i], "-r") == 0)
        {
            UnityFixture.RepeatCount = 2;
            i++;
            if (i < argc)
            {
                if (*(argv[i]) >= '0' && *(argv[i]) <= '9')
                {
                    unsigned int digit = 0;
                    UnityFixture.RepeatCount = 0;
                    while (argv[i][digit] >= '0' && argv[i][digit] <= '9')
                    {
                        UnityFixture.RepeatCount *= 10;
                        UnityFixture.RepeatCount += (unsigned int)argv[i][digit++] - '0';
                    }
                    i++;
                }
            }
        }
        else
        {
            /* ignore unknown parameter */
            i++;
        }
    }
    return 0;
}

void UnityConcludeFixtureTest(void)
{
    if (Unity.CurrentTestIgnored)
    {
        Unity.TestIgnores++;
        UNITY_PRINT_EOL();
    }
    else if (!Unity.CurrentTestFailed)
    {
        if (UnityFixture.Verbose)
        {
            UnityPrint(" ");
            UnityPrint(UnityStrPass);
            UNITY_EXEC_TIME_STOP();
            UNITY_PRINT_EXEC_TIME();
            UNITY_PRINT_EOL();
        }
    }
    else /* Unity.CurrentTestFailed */
    {
        Unity.TestFailures++;
        UNITY_PRINT_EOL();
    }

    Unity.CurrentTestFailed = 0;
    Unity.CurrentTestIgnored = 0;
}
