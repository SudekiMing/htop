/*
htop - Settings.c
(C) 2004-2011 Hisham H. Muhammad
Released under the GNU GPL, see the COPYING file
in the source distribution for its full text.
*/

#include "Settings.h"
#include "Platform.h"

#include "StringUtils.h"
#include "Vector.h"
#include "CRT.h"

#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_DELAY 15

/*{
#include "Process.h"
#include <stdbool.h>

typedef struct {
   int len;
   char** names;
   int* modes;
} MeterColumnSettings;

typedef struct Settings_ {
   char* filename;
   
   MeterColumnSettings columns[2];

   ProcessField* fields;
   int flags;
   int colorScheme;
   int delay;

   int cpuCount;
   int direction;
   ProcessField sortKey;

   bool countCPUsFromZero;
   bool detailedCPUTime;
   bool treeView;
   bool showProgramPath;
   bool hideThreads;
   bool shadowOtherUsers;
   bool showThreadNames;
   bool hideKernelThreads;
   bool hideUserlandThreads;
   bool highlightBaseName;
   bool highlightMegabytes;
   bool highlightThreads;
   bool updateProcessNames;
   bool accountGuestInCPUMeter;
   bool headerMargin;

   char CpuFreq_handler[256]; 
   char CpuTemp_handler[256]; 
   char CpuVCore_l_handler[256];
   char CpuVCore_b_handler[256];
   
   char GpuVCore_handler[256];
   char GpuTemp_handler[256];
   
   char BoardName[128];
   char KernelVersionFull[256];
   char KernelVersionShort[64];
   
   char IP_wlan0[32];
   char IP_wlan1[32];
   char IP_eth0[32];
   char IP_eth1[32];
   
   char eth0_alias[32];
   char eth1_alias[32];
   char wlan0_alias[32];
   char wlan1_alias[32];

   bool changed;
} Settings;

#ifndef Settings_cpuId
#define Settings_cpuId(settings, cpu) ((settings)->countCPUsFromZero ? (cpu) : (cpu)+1)
#endif

}*/

void Settings_delete(Settings* this) {
   free(this->filename);
   free(this->fields);
   for (unsigned int i = 0; i < (sizeof(this->columns)/sizeof(MeterColumnSettings)); i++) {
      String_freeArray(this->columns[i].names);
      free(this->columns[i].modes);
   }
   free(this);
}

static void Settings_readMeters(Settings* this, char* line, int column) {
   char* trim = String_trim(line);
   int nIds;
   char** ids = String_split(trim, ' ', &nIds);
   free(trim);
   this->columns[column].names = ids;
}

static void Settings_readMeterModes(Settings* this, char* line, int column) {
   char* trim = String_trim(line);
   int nIds;
   char** ids = String_split(trim, ' ', &nIds);
   free(trim);
   int len = 0;
   for (int i = 0; ids[i]; i++) {
      len++;
   }
   this->columns[column].len = len;
   int* modes = xCalloc(len, sizeof(int));
   for (int i = 0; i < len; i++) {
      modes[i] = atoi(ids[i]);
   }
   String_freeArray(ids);
   this->columns[column].modes = modes;
}

static void Settings_defaultMeters(Settings* this) {
   int sizes[] = { 3, 3 };
   if (this->cpuCount > 4) {
      sizes[1]++;
   }
   for (int i = 0; i < 2; i++) {
      this->columns[i].names = xCalloc(sizes[i] + 1, sizeof(char*));
      this->columns[i].modes = xCalloc(sizes[i], sizeof(int));
      this->columns[i].len = sizes[i];
   }
   
   int r = 0;
   if (this->cpuCount > 8) {
      this->columns[0].names[0] = xStrdup("LeftCPUs2");
      this->columns[0].modes[0] = BAR_METERMODE;
      this->columns[1].names[r] = xStrdup("RightCPUs2");
      this->columns[1].modes[r++] = BAR_METERMODE;
   } else if (this->cpuCount > 4) {
      this->columns[0].names[0] = xStrdup("LeftCPUs");
      this->columns[0].modes[0] = BAR_METERMODE;
      this->columns[1].names[r] = xStrdup("RightCPUs");
      this->columns[1].modes[r++] = BAR_METERMODE;
   } else {
      this->columns[0].names[0] = xStrdup("AllCPUs");
      this->columns[0].modes[0] = BAR_METERMODE;
   }
   this->columns[0].names[1] = xStrdup("Memory");
   this->columns[0].modes[1] = BAR_METERMODE;
   this->columns[0].names[2] = xStrdup("Swap");
   this->columns[0].modes[2] = BAR_METERMODE;
   
   this->columns[1].names[r] = xStrdup("Tasks");
   this->columns[1].modes[r++] = TEXT_METERMODE;
   this->columns[1].names[r] = xStrdup("LoadAverage");
   this->columns[1].modes[r++] = TEXT_METERMODE;
   this->columns[1].names[r] = xStrdup("Uptime");
   this->columns[1].modes[r++] = TEXT_METERMODE;
   
   this->columns[1].names[r] = xStrdup("CpuTemp");
   this->columns[1].modes[r++] = TEXT_METERMODE;
   this->columns[1].names[r] = xStrdup("CpuFreq");
   this->columns[1].modes[r++] = TEXT_METERMODE;

}

static void readFields(ProcessField* fields, int* flags, const char* line) {
   char* trim = String_trim(line);
   int nIds;
   char** ids = String_split(trim, ' ', &nIds);
   free(trim);
   int i, j;
   *flags = 0;
   for (j = 0, i = 0; i < Platform_numberOfFields && ids[i]; i++) {
      // This "+1" is for compatibility with the older enum format.
      int id = atoi(ids[i]) + 1;
      if (id > 0 && Process_fields[id].name && id < Platform_numberOfFields) {
         fields[j] = id;
         *flags |= Process_fields[id].flags;
         j++;
      }
   }
   fields[j] = NULL_PROCESSFIELD;
   String_freeArray(ids);
}

static bool Settings_read(Settings* this, const char* fileName) {
   FILE* fd;
   
   CRT_dropPrivileges();
   fd = fopen(fileName, "r");
   CRT_restorePrivileges();
   if (!fd)
      return false;
   
   bool didReadMeters = false;
   bool didReadFields = false;
   for (;;) {
      char* line = String_readLine(fd);
      if (!line) {
         break;
      }
      int nOptions;
      char** option = String_split(line, '=', &nOptions);
      free (line);
      if (nOptions < 2) {
         String_freeArray(option);
         continue;
      }
      if (String_eq(option[0], "fields")) {
         readFields(this->fields, &(this->flags), option[1]);
         didReadFields = true;
      } else if (String_eq(option[0], "sort_key")) {
         // This "+1" is for compatibility with the older enum format.
         this->sortKey = atoi(option[1]) + 1;
      } else if (String_eq(option[0], "sort_direction")) {
         this->direction = atoi(option[1]);
      } else if (String_eq(option[0], "tree_view")) {
         this->treeView = atoi(option[1]);
      } else if (String_eq(option[0], "hide_threads")) {
         this->hideThreads = atoi(option[1]);
      } else if (String_eq(option[0], "hide_kernel_threads")) {
         this->hideKernelThreads = atoi(option[1]);
      } else if (String_eq(option[0], "hide_userland_threads")) {
         this->hideUserlandThreads = atoi(option[1]);
      } else if (String_eq(option[0], "shadow_other_users")) {
         this->shadowOtherUsers = atoi(option[1]);
      } else if (String_eq(option[0], "show_thread_names")) {
         this->showThreadNames = atoi(option[1]);
      } else if (String_eq(option[0], "show_program_path")) {
         this->showProgramPath = atoi(option[1]);
      } else if (String_eq(option[0], "highlight_base_name")) {
         this->highlightBaseName = atoi(option[1]);
      } else if (String_eq(option[0], "highlight_megabytes")) {
         this->highlightMegabytes = atoi(option[1]);
      } else if (String_eq(option[0], "highlight_threads")) {
         this->highlightThreads = atoi(option[1]);
      } else if (String_eq(option[0], "header_margin")) {
         this->headerMargin = atoi(option[1]);
      } else if (String_eq(option[0], "expand_system_time")) {
         // Compatibility option.
         this->detailedCPUTime = atoi(option[1]);
      } else if (String_eq(option[0], "detailed_cpu_time")) {
         this->detailedCPUTime = atoi(option[1]);
      } else if (String_eq(option[0], "cpu_count_from_zero")) {
         this->countCPUsFromZero = atoi(option[1]);
      } else if (String_eq(option[0], "update_process_names")) {
         this->updateProcessNames = atoi(option[1]);
      } else if (String_eq(option[0], "account_guest_in_cpu_meter")) {
         this->accountGuestInCPUMeter = atoi(option[1]);
      } else if (String_eq(option[0], "delay")) {
         this->delay = atoi(option[1]);
      } else if (String_eq(option[0], "color_scheme")) {
         this->colorScheme = atoi(option[1]);
         if (this->colorScheme < 0 || this->colorScheme >= LAST_COLORSCHEME) this->colorScheme = 0;
      } else if (String_eq(option[0], "left_meters")) {
         Settings_readMeters(this, option[1], 0);
         didReadMeters = true;
      } else if (String_eq(option[0], "right_meters")) {
         Settings_readMeters(this, option[1], 1);
         didReadMeters = true;
      } else if (String_eq(option[0], "left_meter_modes")) {
         Settings_readMeterModes(this, option[1], 0);
         didReadMeters = true;
      } else if (String_eq(option[0], "right_meter_modes")) {
         Settings_readMeterModes(this, option[1], 1);
         didReadMeters = true;
      } else if (String_eq(option[0], "BoardName")) {
         strcpy(this->BoardName,option[1]);
         didReadMeters = true;
      } else if (String_eq(option[0], "CpuFreq_handler")) {
         strcpy(this->CpuFreq_handler,option[1]);
         didReadMeters = true;
      } else if (String_eq(option[0], "CpuTemp_handler")) {
         strcpy(this->CpuTemp_handler,option[1]);
         didReadMeters = true;
      } else if (String_eq(option[0], "CpuVCore_l_handler")) {
         strcpy(this->CpuVCore_l_handler,option[1]);
         didReadMeters = true;
      } else if (String_eq(option[0], "CpuVCore_b_handler")) {
         strcpy(this->CpuVCore_b_handler,option[1]);
         didReadMeters = true;
      } else if (String_eq(option[0], "GpuVCore_handler")) {
         strcpy(this->GpuVCore_handler,option[1]);
         didReadMeters = true;
      } else if (String_eq(option[0], "GpuTemp_handler")) {
         strcpy(this->GpuTemp_handler,option[1]);
         didReadMeters = true;
      } else if (String_eq(option[0], "eth0_alias")) {
         strcpy(this->eth0_alias,option[1]);
         didReadMeters = true;
      } else if (String_eq(option[0], "eth1_alias")) {
         strcpy(this->eth1_alias,option[1]);
         didReadMeters = true;
      } else if (String_eq(option[0], "wlan0_alias")) {
         strcpy(this->wlan0_alias,option[1]);
         didReadMeters = true;
      } else if (String_eq(option[0], "wlan1_alias")) {
         strcpy(this->wlan1_alias,option[1]);
         didReadMeters = true;
      }  

      String_freeArray(option);
   }
   fclose(fd);
   if (!didReadMeters) {
      Settings_defaultMeters(this);
   }
   return didReadFields;
}

static void writeFields(FILE* fd, ProcessField* fields, const char* name) {
   fprintf(fd, "%s=", name);
   const char* sep = "";
   for (int i = 0; fields[i]; i++) {
      // This "-1" is for compatibility with the older enum format.
      fprintf(fd, "%s%d", sep, (int) fields[i]-1);
      sep = " ";
   }
   fprintf(fd, "\n");
}

static void writeMeters(Settings* this, FILE* fd, int column) {
   const char* sep = "";
   for (int i = 0; i < this->columns[column].len; i++) {
      fprintf(fd, "%s%s", sep, this->columns[column].names[i]);
      sep = " ";
   }
   fprintf(fd, "\n");
}

static void writeMeterModes(Settings* this, FILE* fd, int column) {
   const char* sep = "";
   for (int i = 0; i < this->columns[column].len; i++) {
      fprintf(fd, "%s%d", sep, this->columns[column].modes[i]);
      sep = " ";
   }
   fprintf(fd, "\n");
}

bool Settings_write(Settings* this) {
   FILE* fd;

   CRT_dropPrivileges();
   fd = fopen(this->filename, "w");
   CRT_restorePrivileges();

   if (fd == NULL) {
      return false;
   }
   fprintf(fd, "# Beware! This file is rewritten by htop when settings are changed in the interface.\n");
   fprintf(fd, "# The parser is also very primitive, and not human-friendly.\n");
   writeFields(fd, this->fields, "fields");
   // This "-1" is for compatibility with the older enum format.
   fprintf(fd, "sort_key=%d\n", (int) this->sortKey-1);
   fprintf(fd, "sort_direction=%d\n", (int) this->direction);
   fprintf(fd, "hide_threads=%d\n", (int) this->hideThreads);
   fprintf(fd, "hide_kernel_threads=%d\n", (int) this->hideKernelThreads);
   fprintf(fd, "hide_userland_threads=%d\n", (int) this->hideUserlandThreads);
   fprintf(fd, "shadow_other_users=%d\n", (int) this->shadowOtherUsers);
   fprintf(fd, "show_thread_names=%d\n", (int) this->showThreadNames);
   fprintf(fd, "show_program_path=%d\n", (int) this->showProgramPath);
   fprintf(fd, "highlight_base_name=%d\n", (int) this->highlightBaseName);
   fprintf(fd, "highlight_megabytes=%d\n", (int) this->highlightMegabytes);
   fprintf(fd, "highlight_threads=%d\n", (int) this->highlightThreads);
   fprintf(fd, "tree_view=%d\n", (int) this->treeView);
   fprintf(fd, "header_margin=%d\n", (int) this->headerMargin);
   fprintf(fd, "detailed_cpu_time=%d\n", (int) this->detailedCPUTime);
   fprintf(fd, "cpu_count_from_zero=%d\n", (int) this->countCPUsFromZero);
   fprintf(fd, "update_process_names=%d\n", (int) this->updateProcessNames);
   fprintf(fd, "account_guest_in_cpu_meter=%d\n", (int) this->accountGuestInCPUMeter);
   fprintf(fd, "color_scheme=%d\n", (int) this->colorScheme);
   fprintf(fd, "delay=%d\n", (int) this->delay);
   fprintf(fd, "left_meters="); writeMeters(this, fd, 0);
   fprintf(fd, "left_meter_modes="); writeMeterModes(this, fd, 0);
   fprintf(fd, "right_meters="); writeMeters(this, fd, 1);
   fprintf(fd, "right_meter_modes="); writeMeterModes(this, fd, 1);
   
   fprintf(fd, "# SBC hardware and Kernel specific path.\n");
   fprintf(fd, "# Editable manually.\n");
   fprintf(fd, "BoardName=%s\n", this->BoardName);
   fprintf(fd, "CpuFreq_handler=%s\n", this->CpuFreq_handler);
   fprintf(fd, "CpuTemp_handler=%s\n", this->CpuTemp_handler);
   fprintf(fd, "CpuVCore_l_handler=%s\n", this->CpuVCore_l_handler); 
   fprintf(fd, "CpuVCore_b_handler=%s\n", this->CpuVCore_b_handler);    
   fprintf(fd, "GpuVCore_handler=%s\n", this->GpuVCore_handler); 
   fprintf(fd, "GpuTemp_handler=%s\n", this->GpuTemp_handler); 
   
   fprintf(fd, "# Wlan / Eth alias\n");
   fprintf(fd, "eth0_alias=%s\n",this->eth0_alias);
   fprintf(fd, "eth1_alias=%s\n",this->eth1_alias);
   fprintf(fd, "wlan0_alias=%s\n",this->wlan0_alias);
   fprintf(fd, "wlan1_alias=%s\n",this->wlan1_alias);

   fclose(fd);
   return true;
}

Settings* Settings_new(int cpuCount) {
  
   Settings* this = xCalloc(1, sizeof(Settings));

   this->sortKey = PERCENT_CPU;
   this->direction = 1;
   this->hideThreads = false;
   this->shadowOtherUsers = false;
   this->showThreadNames = false;
   this->hideKernelThreads = false;
   this->hideUserlandThreads = false;
   this->treeView = false;
   this->highlightBaseName = false;
   this->highlightMegabytes = false;
   this->detailedCPUTime = false;
   this->countCPUsFromZero = false;
   this->updateProcessNames = false;
   this->cpuCount = cpuCount;
   this->showProgramPath = true;
   this->highlightThreads = true;
   
   this->fields = xCalloc(Platform_numberOfFields+1, sizeof(ProcessField));
   // TODO: turn 'fields' into a Vector,
   // (and ProcessFields into proper objects).
   this->flags = 0;
   ProcessField* defaults = Platform_defaultFields;
   for (int i = 0; defaults[i]; i++) {
      this->fields[i] = defaults[i];
      this->flags |= Process_fields[defaults[i]].flags;
   }

   char* legacyDotfile = NULL;
   char* rcfile = getenv("HTOPRC");
   if (rcfile) {
      this->filename = xStrdup(rcfile);
   } else {
      const char* home = getenv("HOME");
      if (!home) home = "";
      const char* xdgConfigHome = getenv("XDG_CONFIG_HOME");
      char* configDir = NULL;
      char* htopDir = NULL;
      if (xdgConfigHome) {
         this->filename = String_cat(xdgConfigHome, "/htop/htoprc");
         configDir = xStrdup(xdgConfigHome);
         htopDir = String_cat(xdgConfigHome, "/htop");
      } else {
         this->filename = String_cat(home, "/.config/htop/htoprc");
         configDir = String_cat(home, "/.config");
         htopDir = String_cat(home, "/.config/htop");
      }
      legacyDotfile = String_cat(home, "/.htoprc");
      
      CRT_dropPrivileges();
      (void) mkdir(configDir, 0700);
      (void) mkdir(htopDir, 0700);
      free(htopDir);
      free(configDir);
      struct stat st;
      int err = lstat(legacyDotfile, &st);
      if (err || S_ISLNK(st.st_mode)) {
         free(legacyDotfile);
         legacyDotfile = NULL;
      }
      CRT_restorePrivileges();
   }
   this->colorScheme = 0;
   this->changed = false;
   this->delay = DEFAULT_DELAY;
   bool ok = false;
   if (legacyDotfile) {
      ok = Settings_read(this, legacyDotfile);
      if (ok) {
         // Transition to new location and delete old configuration file
         if (Settings_write(this))
            unlink(legacyDotfile);
      }
      free(legacyDotfile);
   }
   if (!ok) {
      ok = Settings_read(this, this->filename);
   }
   if (!ok) {
      this->changed = true;
      // TODO: how to get SYSCONFDIR correctly through Autoconf?
      char* systemSettings = String_cat(SYSCONFDIR, "/htoprc");
      ok = Settings_read(this, systemSettings);
      free(systemSettings);
   }
   if (!ok) {
      Settings_defaultMeters(this);
      this->hideKernelThreads = true;
      this->highlightMegabytes = true;
      this->highlightThreads = true;
      this->headerMargin = true;
   }
   return this;
}

void Settings_invertSortOrder(Settings* this) {
   if (this->direction == 1)
      this->direction = -1;
   else
      this->direction = 1;
}
