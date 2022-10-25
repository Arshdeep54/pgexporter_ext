/*
 * Copyright (C) 2022 Red Hat
 * 
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 * 
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* pgexporter */
#include <pgexporter_ext.h>
#include <utils.h>

/* system */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_LINUX
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#endif

/* PostgreSQL */
#include "postgres.h"
#include "fmgr.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "nodes/execnodes.h"
#include "utils/builtins.h"

PG_MODULE_MAGIC;

#define OS_INFO_NUMBER        7
#define OS_INFO_NAME          0
#define OS_INFO_VERSION       1
#define OS_INFO_ARCHITECTURE  2
#define OS_INFO_HOST_NAME     3
#define OS_INFO_DOMAIN_NAME   4
#define OS_INFO_PROCESS_COUNT 5
#define OS_INFO_UP_SINCE      6

#define CPU_INFO_NUMBER       8
#define CPU_INFO_VENDOR       0
#define CPU_INFO_MODEL        1
#define CPU_INFO_CORES        2
#define CPU_INFO_CLOCK_SPEED  3
#define CPU_INFO_CACHEL1D     4
#define CPU_INFO_CACHEL1I     5
#define CPU_INFO_CACHEL2      6
#define CPU_INFO_CACHEL3      7

#define MEMORY_INFO_NUMBER       7
#define MEMORY_INFO_TOTAL_MEMORY 0
#define MEMORY_INFO_USED_MEMORY  1
#define MEMORY_INFO_FREE_MEMORY  2
#define MEMORY_INFO_SWAP_TOTAL   3
#define MEMORY_INFO_SWAP_USED    4
#define MEMORY_INFO_SWAP_FREE    5
#define MEMORY_INFO_CACHE_TOTAL  6

static void     os_info(Tuplestorestate* tupstore, TupleDesc tupdesc);
static bool     read_processes(int* process_count);
static void     cpu_info(Tuplestorestate* tupstore, TupleDesc tupdesc);
static int      read_cpu_cache_size(const char *file);
static void     memory_info(Tuplestorestate* tupstore, TupleDesc tupdesc);
static uint64_t kb_to_bytes(char* s);

void _PG_init(void)
{
}

void _PG_fini(void)
{
}

PG_FUNCTION_INFO_V1(pgexporter_version);

PG_FUNCTION_INFO_V1(pgexporter_used_space);
PG_FUNCTION_INFO_V1(pgexporter_free_space);
PG_FUNCTION_INFO_V1(pgexporter_total_space);

PG_FUNCTION_INFO_V1(pgexporter_os_info);
PG_FUNCTION_INFO_V1(pgexporter_cpu_info);
PG_FUNCTION_INFO_V1(pgexporter_memory_info);

Datum
pgexporter_version(PG_FUNCTION_ARGS)
{
   Datum version;
   char v[1024];

   memset(&v, 0, sizeof(v));
   snprintf(&v[0], sizeof(v), "pgexporter_ext %s", VERSION);

   ereport(INFO, (errmsg("pgexporter_ext %s", VERSION)));
   ereport(INFO, (errmsg("  Homepage: %s", PGEXPORTER_EXT_HOMEPAGE)));
   ereport(INFO, (errmsg("  Issues: %s", PGEXPORTER_EXT_ISSUES)));

   version = CStringGetTextDatum(v);

   PG_RETURN_CSTRING(version);
}

Datum
pgexporter_used_space(PG_FUNCTION_ARGS)
{
   unsigned long size;
   char* directory = text_to_cstring(PG_GETARG_TEXT_PP(0));

   size = pgexporter_ext_directory_size(directory);

   PG_RETURN_INT64(size);
}

Datum
pgexporter_free_space(PG_FUNCTION_ARGS)
{
   unsigned long size;
   char* directory = text_to_cstring(PG_GETARG_TEXT_PP(0));

   size = pgexporter_ext_free_space(directory);

   PG_RETURN_INT64(size);
}

Datum
pgexporter_total_space(PG_FUNCTION_ARGS)
{
   unsigned long size;
   char* directory = text_to_cstring(PG_GETARG_TEXT_PP(0));

   size = pgexporter_ext_total_space(directory);

   PG_RETURN_INT64(size);
}

Datum
pgexporter_os_info(PG_FUNCTION_ARGS)
{
   ReturnSetInfo* rsinfo = (ReturnSetInfo*)fcinfo->resultinfo;
   TupleDesc tupdesc;
   Tuplestorestate* tupstore;
   MemoryContext per_query_ctx;
   MemoryContext oldcontext;

   per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
   oldcontext = MemoryContextSwitchTo(per_query_ctx);

   if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
   {
      elog(ERROR, "Must be a return row type");
   }

   tupstore = tuplestore_begin_heap(true, false, work_mem);
   rsinfo->returnMode = SFRM_Materialize;
   rsinfo->setResult = tupstore;
   rsinfo->setDesc = tupdesc;

   MemoryContextSwitchTo(oldcontext);

   os_info(tupstore, tupdesc);

   tuplestore_donestoring(tupstore);

   return (Datum)0;
}

Datum
pgexporter_cpu_info(PG_FUNCTION_ARGS)
{
   ReturnSetInfo* rsinfo = (ReturnSetInfo*)fcinfo->resultinfo;
   TupleDesc tupdesc;
   Tuplestorestate* tupstore;
   MemoryContext per_query_ctx;
   MemoryContext oldcontext;

   per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
   oldcontext = MemoryContextSwitchTo(per_query_ctx);

   if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
   {
      elog(ERROR, "Must be a return row type");
   }

   tupstore = tuplestore_begin_heap(true, false, work_mem);
   rsinfo->returnMode = SFRM_Materialize;
   rsinfo->setResult = tupstore;
   rsinfo->setDesc = tupdesc;

   MemoryContextSwitchTo(oldcontext);

   cpu_info(tupstore, tupdesc);

   tuplestore_donestoring(tupstore);

   return (Datum)0;
}

Datum
pgexporter_memory_info(PG_FUNCTION_ARGS)
{
   ReturnSetInfo* rsinfo = (ReturnSetInfo*)fcinfo->resultinfo;
   TupleDesc tupdesc;
   Tuplestorestate* tupstore;
   MemoryContext per_query_ctx;
   MemoryContext oldcontext;

   per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
   oldcontext = MemoryContextSwitchTo(per_query_ctx);

   if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
   {
      elog(ERROR, "Must be a return row type");
   }

   tupstore = tuplestore_begin_heap(true, false, work_mem);
   rsinfo->returnMode = SFRM_Materialize;
   rsinfo->setResult = tupstore;
   rsinfo->setDesc = tupdesc;

   MemoryContextSwitchTo(oldcontext);

   memory_info(tupstore, tupdesc);

   tuplestore_donestoring(tupstore);

   return (Datum)0;
}

static void
os_info(Tuplestorestate* tupstore, TupleDesc tupdesc)
{
#ifdef HAVE_LINUX
   struct utsname uts;
   struct sysinfo s_info;
   Datum values[OS_INFO_NUMBER];
   bool nulls[OS_INFO_NUMBER];
   char host_name[MAXPGPATH];
   char domain_name[MAXPGPATH];
   char version[MAXPGPATH];
   char architecture[MAXPGPATH];
   char os_name[MAXPGPATH];
   FILE* os_info_file;
   char* line_buf = NULL;
   size_t line_buf_size = 0;
   ssize_t line_size;
   int process_count = 0;

   memset(nulls, 0, sizeof(nulls));
   memset(host_name, 0, MAXPGPATH);
   memset(domain_name, 0, MAXPGPATH);
   memset(version, 0, MAXPGPATH);
   memset(architecture, 0, MAXPGPATH);
   memset(os_name, 0, MAXPGPATH);

   if (uname(&uts) == 0)
   {
      sprintf(version, "%s %s", uts.sysname, uts.release);
      memcpy(architecture, uts.machine, strlen(uts.machine));
   }
   else
   {
      nulls[OS_INFO_VERSION]  = true;
      nulls[OS_INFO_ARCHITECTURE] = true;
   }

   if (gethostname(host_name, sizeof(host_name)) != 0)
   {
      nulls[OS_INFO_HOST_NAME] = true;
   }

   if (getdomainname(domain_name, sizeof(domain_name)) != 0)
   {
      nulls[OS_INFO_DOMAIN_NAME] = true;
   }

   os_info_file = fopen("/etc/os-release", "r");

   if (!os_info_file)
   {
      nulls[OS_INFO_NAME] = true;
   }
   else
   {
      line_size = getline(&line_buf, &line_buf_size, os_info_file);

      while (line_size >= 0)
      {
         int len = strlen(line_buf);
         if (strstr(line_buf, "PRETTY_NAME=") != NULL)
            memcpy(os_name, (line_buf + strlen("PRETTY_NAME=")), (len - strlen("PRETTY_NAME=")));

         if (line_buf != NULL)
         {
            free(line_buf);
            line_buf = NULL;
         }

         line_size = getline(&line_buf, &line_buf_size, os_info_file);
      }

      if (line_buf != NULL)
      {
         free(line_buf);
         line_buf = NULL;
      }

      fclose(os_info_file);
   }

   if (read_processes(&process_count))
   {
      values[OS_INFO_PROCESS_COUNT] = process_count;
   }
   else
   {
      nulls[OS_INFO_PROCESS_COUNT] = true;
   }

   if (sysinfo(&s_info) == 0)
   {
      values[OS_INFO_UP_SINCE] = Int32GetDatum((int)s_info.uptime);
   }
   else
   {
      nulls[OS_INFO_UP_SINCE] = true;
   }

   values[OS_INFO_NAME] = CStringGetTextDatum(os_name);
   values[OS_INFO_VERSION] = CStringGetTextDatum(version);
   values[OS_INFO_ARCHITECTURE] = CStringGetTextDatum(architecture);
   values[OS_INFO_HOST_NAME] = CStringGetTextDatum(host_name);
   values[OS_INFO_DOMAIN_NAME] = CStringGetTextDatum(domain_name);

   tuplestore_putvalues(tupstore, tupdesc, values, nulls);

#else

   Datum values[OS_INFO_NUMBER];
   bool nulls[OS_INFO_NUMBER];

   memset(nulls, 0, sizeof(nulls));

   nulls[OS_INFO_NAME] = true;
   nulls[OS_INFO_VERSION] = true;
   nulls[OS_INFO_ARCHITECTURE] = true;
   nulls[OS_INFO_HOST_NAME] = true;
   nulls[OS_INFO_DOMAIN_NAME] = true;
   nulls[OS_INFO_PROCESS_COUNT] = true;
   nulls[OS_INFO_UP_SINCE] = true;

   tuplestore_putvalues(tupstore, tupdesc, values, nulls);

#endif
}

static bool
read_processes(int* process_count)
{
#ifdef HAVE_LINUX
   DIR* dir;
   struct dirent* entry;
   int pc = 0;

   *process_count = 0;

   if (!(dir = opendir("/proc")))
   {
      goto error;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_DIR)
      {
         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         {
            continue;
         }

         if (!isdigit(entry->d_name[0]))
         {
            continue;
         }

         pc++;
      }
   }

   closedir(dir);

   *process_count = pc;

   return true;

error:

   return false;

#else
   *process_count = 0;
   return false;
#endif
}

static void
cpu_info(Tuplestorestate* tupstore, TupleDesc tupdesc)
{
#ifdef HAVE_LINUX
   const int max_length = 1024;
   char buffer[max_length];
   char* col;
   FILE* cpu_info_file;
   Datum values[CPU_INFO_NUMBER];
   bool nulls[CPU_INFO_NUMBER];
   char vendor_id[MAXPGPATH];
   char model_name[MAXPGPATH];
   int l1dcache_size_kb = 0;
   int l1icache_size_kb = 0;
   int l2cache_size_kb = 0;
   int l3cache_size_kb = 0;
   int cpu_cores = 0;
   float cpu_hz;
   uint64_t cpu_freq;

   memset(nulls, 0, sizeof(nulls));
   memset(vendor_id, 0, MAXPGPATH);
   memset(model_name, 0, MAXPGPATH);

   l1dcache_size_kb = read_cpu_cache_size("/sys/devices/system/cpu/cpu0/cache/index0/size");
   l1icache_size_kb = read_cpu_cache_size("/sys/devices/system/cpu/cpu0/cache/index1/size");
   l2cache_size_kb = read_cpu_cache_size("/sys/devices/system/cpu/cpu0/cache/index2/size");
   l3cache_size_kb = read_cpu_cache_size("/sys/devices/system/cpu/cpu0/cache/index3/size");

   cpu_info_file = fopen("/proc/cpuinfo", "r");

   if (!cpu_info_file)
   {
      goto error;
   }
   else
   {
      while (fgets(&buffer[0], max_length, cpu_info_file) != NULL)
      {
         if (strlen(buffer) > 0)
         {
            col = strstr(buffer, ":");
            if (col != NULL)
            {
               if (strstr(&buffer[0], "vendor_id") != NULL)
               {
                  memcpy(vendor_id, col + 1, strlen(col + 1));
               }
               else if (strstr(&buffer[0], "model name") != NULL)
               {
                  memcpy(model_name, col + 1, strlen(col + 1));
               }
               else if (strstr(&buffer[0], "cpu cores") != NULL)
               {
                  cpu_cores = atoi(col + 1);
               }
               else if (strstr(&buffer[0], "cpu MHz") != NULL)
               {
                  cpu_hz = atof(col + 1);
                  cpu_freq = (cpu_hz * 1000000);
               }
            }
         }
      }

      fclose(cpu_info_file);

      values[CPU_INFO_VENDOR] = CStringGetTextDatum(vendor_id);
      values[CPU_INFO_MODEL] = CStringGetTextDatum(model_name);
      values[CPU_INFO_CORES] = Int32GetDatum(cpu_cores);
      values[CPU_INFO_CLOCK_SPEED] = Int64GetDatumFast(cpu_freq);
      values[CPU_INFO_CACHEL1D] = Int32GetDatum(l1dcache_size_kb);
      values[CPU_INFO_CACHEL1I] = Int32GetDatum(l1icache_size_kb);
      values[CPU_INFO_CACHEL2] = Int32GetDatum(l2cache_size_kb);
      values[CPU_INFO_CACHEL3] = Int32GetDatum(l3cache_size_kb);

      tuplestore_putvalues(tupstore, tupdesc, values, nulls);
   }

   return;

error:

   nulls[CPU_INFO_VENDOR] = true;
   nulls[CPU_INFO_MODEL] = true;
   nulls[CPU_INFO_CORES] = true;
   nulls[CPU_INFO_CLOCK_SPEED] = true;
   nulls[CPU_INFO_CACHEL1D] = true;
   nulls[CPU_INFO_CACHEL1I] = true;
   nulls[CPU_INFO_CACHEL2] = true;
   nulls[CPU_INFO_CACHEL3] = true;

   tuplestore_putvalues(tupstore, tupdesc, values, nulls);

#else

   Datum values[CPU_INFO_NUMBER];
   bool nulls[CPU_INFO_NUMBER];

   memset(nulls, 0, sizeof(nulls));

   nulls[CPU_INFO_VENDOR] = true;
   nulls[CPU_INFO_MODEL] = true;
   nulls[CPU_INFO_CORES] = true;
   nulls[CPU_INFO_CLOCK_SPEED] = true;
   nulls[CPU_INFO_CACHEL1D] = true;
   nulls[CPU_INFO_CACHEL1I] = true;
   nulls[CPU_INFO_CACHEL2] = true;
   nulls[CPU_INFO_CACHEL3] = true;

   tuplestore_putvalues(tupstore, tupdesc, values, nulls);

#endif
}

static int
read_cpu_cache_size(const char* file)
{
   FILE* fp;
   int max_length = 1024;
   int length;
   char buffer[max_length];
   int cache_size = 0;

   fp = fopen(file, "r");
   if (!fp)
   {
      cache_size = 0;
   }
   else
   {
      if (fgets(&buffer[0], max_length, fp) != NULL)
      {
         length = strlen(buffer);
         for (int i = 0; i < length; i++)
         {
            if (!isdigit(buffer[i]))
            {
               buffer[i] = '\0';
               break;
            }
         }

         cache_size = atoi(buffer);
      }

      fclose(fp);
   }

   return cache_size;
}

static void
memory_info(Tuplestorestate* tupstore, TupleDesc tupdesc)
{
#ifdef HAVE_LINUX
   const int max_length = 1024;
   char buffer[max_length];
   FILE* memory_file;
   Datum values[MEMORY_INFO_NUMBER];
   bool nulls[MEMORY_INFO_NUMBER];
   uint64 total_memory_bytes = 0;
   uint64 available_memory_bytes = 0;
   uint64 used_memory_bytes = 0;
   uint64 free_memory_bytes = 0;
   uint64 swap_total_bytes = 0;
   uint64 swap_used_bytes = 0;
   uint64 swap_free_bytes = 0;
   uint64 cached_bytes = 0;

   memset(nulls, 0, sizeof(nulls));

   memory_file = fopen("/proc/meminfo", "r");

   if (!memory_file)
   {
      goto error;
   }
   else
   {
      while (fgets(&buffer[0], max_length, memory_file) != NULL)
      {
         if (strlen(buffer) > 0)
         {
            if (strstr(&buffer[0], "MemTotal") != NULL)
            {
               total_memory_bytes = kb_to_bytes(&buffer[0]);
            }
            else if (strstr(&buffer[0], "MemFree") != NULL)
            {
               free_memory_bytes = kb_to_bytes(&buffer[0]);
            }
            else if (strstr(&buffer[0], "MemAvailable") != NULL)
            {
               available_memory_bytes = kb_to_bytes(&buffer[0]);
            }
            else if (strstr(&buffer[0], "SwapTotal") != NULL)
            {
               swap_total_bytes = kb_to_bytes(&buffer[0]);
            }
            else if (strstr(&buffer[0], "SwapFree") != NULL)
            {
               swap_free_bytes = kb_to_bytes(&buffer[0]);
            }
            else if (strstr(&buffer[0], "SwapCached") != NULL)
            {
               /* Skip */
            }
            else if (strstr(&buffer[0], "Cached") != NULL)
            {
               cached_bytes = kb_to_bytes(&buffer[0]);
            }
         }
      }

      used_memory_bytes = total_memory_bytes - available_memory_bytes;

      fclose(memory_file);

      values[MEMORY_INFO_TOTAL_MEMORY] = Int64GetDatumFast(total_memory_bytes);
      values[MEMORY_INFO_USED_MEMORY] = Int64GetDatumFast(used_memory_bytes);
      values[MEMORY_INFO_FREE_MEMORY] = Int64GetDatumFast(free_memory_bytes);
      values[MEMORY_INFO_SWAP_TOTAL] = Int64GetDatumFast(swap_total_bytes);
      values[MEMORY_INFO_SWAP_USED] = Int64GetDatumFast(swap_used_bytes);
      values[MEMORY_INFO_SWAP_FREE] = Int64GetDatumFast(swap_free_bytes);
      values[MEMORY_INFO_CACHE_TOTAL] = Int64GetDatumFast(cached_bytes);

      tuplestore_putvalues(tupstore, tupdesc, values, nulls);
   }

   return;

error:

   nulls[MEMORY_INFO_TOTAL_MEMORY] = true;
   nulls[MEMORY_INFO_USED_MEMORY] = true;
   nulls[MEMORY_INFO_FREE_MEMORY] = true;
   nulls[MEMORY_INFO_SWAP_TOTAL] = true;
   nulls[MEMORY_INFO_SWAP_USED] = true;
   nulls[MEMORY_INFO_SWAP_FREE] = true;
   nulls[MEMORY_INFO_CACHE_TOTAL] = true;

   tuplestore_putvalues(tupstore, tupdesc, values, nulls);

#else

   Datum values[MEMORY_INFO_NUMBER];
   bool nulls[MEMORY_INFO_NUMBER];

   memset(nulls, 0, sizeof(nulls));

   nulls[MEMORY_INFO_TOTAL_MEMORY] = true;
   nulls[MEMORY_INFO_USED_MEMORY] = true;
   nulls[MEMORY_INFO_FREE_MEMORY] = true;
   nulls[MEMORY_INFO_SWAP_TOTAL] = true;
   nulls[MEMORY_INFO_SWAP_USED] = true;
   nulls[MEMORY_INFO_SWAP_FREE] = true;
   nulls[MEMORY_INFO_CACHE_TOTAL] = true;

   tuplestore_putvalues(tupstore, tupdesc, values, nulls);

#endif
}

static uint64_t
kb_to_bytes(char* s)
{
   uint64_t value = 0;
   char* col;
   char* trimmed;
   char* token;

   col = strstr(s, ":");
   trimmed = pgexporter_ext_trim_whitespace(col + 1);

   token = strtok(trimmed, " ");
   value = atoll(token);

   token = strtok(NULL, " ");
   if (!strcasecmp(token, "KB"))
   {
      value = value * 1024;
   }
   else if (!strcasecmp(token, "MB"))
   {
      value = value * 1024 * 1024;
   }
   else if (!strcasecmp(token, "GB"))
   {
      value = value * 1024 * 1024 * 1024;
   }

   free(trimmed);

   return value;
}
