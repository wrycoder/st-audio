#include "splice.h"
#include <strsafe.h>
#include <assert.h>
#include <sys/stat.h>

/**
 * SoX-dependent Functions
 *
 */

typedef struct {
  char * filename;

  /* fopts */
  char const * filetype;
  sox_signalinfo_t signal;
  sox_encodinginfo_t encoding;
  sox_oob_t oob;
  sox_bool no_glob;

  sox_format_t * ft; /* libSoX file descriptor */
  uint64_t volume_clips;
} file_t;

static file_t * * files = NULL; /* Array tracking input and output files */
#define ofile files[file_count - 1]
static size_t file_count = 0;
static size_t input_count = 0;
static size_t output_count = 0;

static int success = 0;

static void init_file(file_t * f)
{
  memset(f, 0, sizeof(*f));
  sox_init_encodinginfo(&f->encoding);
}

static sox_format_t * in, * out;

wchar_t const * str_time(double seconds)
{
  static wchar_t string[16][50];
  size_t cchDest = 50;
  static int i;
  LPCTSTR pszFormatWithHours = L"%02i:%02i:%05.2f";
  LPCTSTR pszFormat = L"%02i:%05.2f";
  int hours, mins = seconds / 60;
  seconds -= mins * 60;
  hours = mins / 60;
  mins -= hours * 60;
  i = (i+1) & 15;
  if (hours > 0)
  {
    StringCchPrintfW(string[i], cchDest * sizeof(wchar_t), pszFormatWithHours, hours, mins, seconds);
  } else {
    StringCchPrintfW(string[i], cchDest * sizeof(wchar_t), pszFormat, mins, seconds);
  }
  return string[i];
}

void show_name_and_runtime(sox_format_t * in)
{
  double secs;
  uint64_t ws;
  PWSTR msgbuf, filenamebuf;

  wchar_t *msg_template = L"%s ... %-15.15s\n";
  size_t buffer_size = (wcslen(msg_template) + 20) * sizeof(WCHAR);
  msgbuf = (PWSTR)CoTaskMemAlloc(buffer_size);
  ws = in->signal.length / max(in->signal.channels, 1);
  secs = (double)ws / max(in->signal.rate, 1);
  int filename_length = MultiByteToWideChar(CP_ACP, 0, in->filename, -1, NULL, 0);
  filenamebuf = (PWSTR)CoTaskMemAlloc(filename_length * sizeof(WCHAR));
  MultiByteToWideChar(CP_ACP, 0, in->filename, -1, filenamebuf, filename_length);
  StringCbPrintfW(msgbuf, buffer_size, msg_template, filenamebuf, str_time(secs));
  MessageBox(NULL, msgbuf, L"FILE DETAILS", MB_OK);
  CoTaskMemFree(filenamebuf);
  CoTaskMemFree(msgbuf);
}

void trim_silence(wchar_t * filename, char * duration, char * threshold)
{
  wchar_t szNewPath[MAX_PATH * sizeof(wchar_t)];
  unsigned long sample_count = 0L;
  sox_effects_chain_t * chain;
  sox_effect_t * e;
  int sox_result = SOX_SUCCESS;
  char * args[10];

  in = sox_open_read(convert_pwstr_to_const_char(filename), NULL, NULL, NULL);
  if (in == NULL)
  {
    report_error(NULL, errno, __FILE__, __LINE__);
    cleanup();
    return;
  }
  out = sox_open_write("temp.wav", &in->signal, NULL, NULL, NULL, NULL);
  if (out == NULL)
  {
    report_error(NULL, errno, __FILE__, __LINE__);
    cleanup();
    return;
  }
  chain = sox_create_effects_chain(&in->encoding, &out->encoding);
  e = sox_create_effect(sox_find_effect("input"));
  args[0] = (char *)in, assert(sox_effect_options(e, 1, args) == SOX_SUCCESS);
  assert(sox_add_effect(chain, e, &in->signal, &in->signal) == SOX_SUCCESS);
  free(e);
  e = sox_create_effect(sox_find_effect("reverse"));
  assert(sox_add_effect(chain, e, &in->signal, &in->signal) == SOX_SUCCESS);
  free(e);
  e = sox_create_effect(sox_find_effect("silence"));
  args[0] = "1";
  args[1] = duration;
  args[2] = threshold;
  assert(sox_effect_options(e, 3, args) == SOX_SUCCESS);
  sox_result = sox_add_effect(chain, e, &in->signal, &in->signal);
  if (sox_result != SOX_SUCCESS)
  {
    report_error(NULL, sox_result, __FILE__, __LINE__);
    cleanup();
    return;
  }
  free(e);
  e = sox_create_effect(sox_find_effect("reverse"));
  assert(sox_add_effect(chain, e, &in->signal, &in->signal) == SOX_SUCCESS);
  free(e);
  // Now, trim silence from the beginning of the file...
  e = sox_create_effect(sox_find_effect("silence"));
  assert(sox_effect_options(e, 3, args) == SOX_SUCCESS);
  sox_result = sox_add_effect(chain, e, &in->signal, &in->signal);
  if (sox_result != SOX_SUCCESS)
  {
    report_error(NULL, sox_result, __FILE__, __LINE__);
    cleanup();
    return;
  }
  free(e);
  e = sox_create_effect(sox_find_effect("output"));
  args[0] = (char *)out, assert(sox_effect_options(e, 1, args) == SOX_SUCCESS);
  assert(sox_add_effect(chain, e, &in->signal, &in->signal) == SOX_SUCCESS);
  free(e);

  sox_flow_effects(chain, NULL, NULL);
  sox_delete_effects_chain(chain);
  sox_close(out);
  sox_close(in);
  StringCchPrintf(szNewPath, sizeof(szNewPath)/sizeof(szNewPath[0]), TEXT("%s"), convert_pwstr_to_const_char(filename));
  CopyFileA("temp.wav", szNewPath, FALSE);
  DeleteFileA("temp.wav");
}

/*
 * Splice audio files
 *
 * This is how I got it to work in zsh, with the Linux/MacOS sox executable...
 *
 *  sox babayaga.wav greatgate.wav _merged.wav splice -q `soxi -D babayaga.wav`,0.1
 *
 * NOTE: The sample files are from Mussorgsky's _Pictures_at_an_Exhibition_, at
 * the transition from "The Hut on Fowl's Legs" to "The Great Gate at Kiev."
 *
 * I think example4.c in the libsox package is closest to what I am trying to do.
 */
void splice(wchar_t * * wfile_names, int file_count)
{
  sox_effect_t * e;
  sox_format_t * in_file, * out_file;
  sox_signalinfo_t in_signal, out_signal;
  char * * file_names;
  sox_sample_t samples[MAXIMUM_SAMPLES]; /* Temporary store while copying */
  size_t number_read;
  const char * current_filename;
  int sox_result = SOX_SUCCESS;
  int i;

  file_names = malloc(sizeof(char *));
  for (i = 0; i < file_count; ++i)
  {
    current_filename = convert_pwstr_to_const_char(wfile_names[i]);
    if (current_filename != NULL)
    {
      report_current_action(NULL, "I have found a filename");
      report_current_action(NULL, current_filename);
    } else {
      report_error(NULL, ST_ERROR, __FILE__, __LINE__);
      cleanup();
      return;
    }
    file_names[i] = (char *)malloc(sizeof(char) * (strlen(current_filename) + 1));
    StringCbCopyA(file_names[i], strlen(current_filename), current_filename);
  }

  report_current_action(NULL, file_names[0]);
  in_file = sox_open_read(file_names[0], &in_signal, NULL, NULL);
  if (in_file == NULL)
  {
    report_error(NULL, SOX_LIB_ERROR, __FILE__, __LINE__);
    cleanup();
    return;
  }
  out_signal = in_signal;
  out_file = sox_open_write(DEFAULT_OUTPUT_FILENAME, &out_signal, NULL, NULL, NULL, NULL);
  if (out_file == NULL)
  {
    report_error(NULL, SOX_LIB_ERROR, __FILE__, __LINE__);
    cleanup();
    return;
  }
  if ( (in_file->signal.channels != out_file->signal.channels) ||
        (in_file->signal.rate != out_file->signal.rate) )
  {
    sox_close(out_file);
    report_error(NULL, USER_ERROR, __FILE__, __LINE__);
    cleanup();
    return;
  }

  e = sox_create_effect(sox_find_effect("splice"));
  char * splice_options[] = { "fade", "q", "leeway", "0.5", NULL };
  sox_effect_options(e, file_count, file_names);
  sox_effect_options(e, 4, splice_options);
  sox_add_effect(in_file, e, &in_signal, &out_signal);
  sox_flow_effects(e, in_file, out_file);

  sox_delete_effects_chain(e);
  sox_close(in_file);
  sox_close(out_file);

  for (i = 0; i < file_count; ++i)
  {
    free((char*)(file_names[i]));
  }

  if (file_names != NULL)
    free(file_names);
}

/* All done; tidy up... */
int cleanup()
{
  STRSAFE_LPSTR sox_wildcard = L"libSoX.tmp*";
  TCHAR szTempFileWildcard[MAX_PATH * sizeof(wchar_t)];
  TCHAR szCurrentTempFileName[MAX_PATH * sizeof(wchar_t)];
  WIN32_FIND_DATA fdFile;
  HANDLE hFind = NULL;
  size_t i;

  if (in != NULL) sox_close(in);
  if (out != NULL) sox_close(out);

  /* Close the input and output files before exiting. */
  /*
  for (i = 0; i < input_count; i++)
  {
    if (files[i]->ft)
    {
      sox_close(files[i]->ft);
    }
    free(files[i]->filename);
    free(files[i]);
  }
  */
  /*
  if (file_count)
  {
    if (ofile->ft)
    {
      if (!success && ofile->ft->io_type == lsx_io_file)
      { // If we failed part way through
        struct stat st; // writing a normal file, remove it.
        if (!stat(ofile->ft->filename, &st) &&
            (st.st_mode & S_IFMT) == S_IFREG)
          unlink(ofile->ft->filename);
      }
      sox_close(ofile->ft); // Assume we can unlink a file before closing it.
    }
    free(ofile->filename);
    free(ofile);
  }
  */

  if (files != NULL)
    free(files);

  if (sox_quit_called == 0)
  {
    sox_quit();
    sox_quit_called = 1;
  }
  GetTempPathW(MAX_PATH, szTempFileWildcard);
  StringCbCatW(szTempFileWildcard, MAX_PATH, sox_wildcard);
  if((hFind = FindFirstFile(szTempFileWildcard, &fdFile)) != INVALID_HANDLE_VALUE)
  {
    do
    {
      /* FindFirstFile will always return "." and ".."
       * as the first two directories. */
      if(strcmp(fdFile.cFileName, ".") != 0
        && strcmp(fdFile.cFileName, "..") != 0)
      {
        GetTempPathW(MAX_PATH, szCurrentTempFileName);
        StringCbCatW(szCurrentTempFileName, MAX_PATH, fdFile.cFileName);
        DeleteFileW(szCurrentTempFileName);
      }
    }
    while(FindNextFile(hFind, &fdFile)); /* Find the next file. */
  }
  return 0;
}

