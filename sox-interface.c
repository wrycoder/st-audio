/* sox-interface.c
 *
 * (c) 2023 Michael Toulouse
 *
 * SoX-dependent functions supporting the Splice application.
 *
 */

#include "splice.h"
#include <strsafe.h>
#include <assert.h>
#include <sys/stat.h>
#include "xmalloc.h"

typedef struct {
  enum {Cosine_2, Cosine_4, Triangular} fade_type;
  unsigned nsplices;        /* Number of splices requested */
  struct {
    char * str;             /* Command-line argument to parse for this splice */
    uint64_t overlap;       /* Number of samples to overlap */
    uint64_t search;        /* Number of samples to search */
    uint64_t start;         /* Start splicing when in_pos equals this */
  } * splices;

  uint64_t in_pos;          /* Number of samples read from the input stream */
  unsigned splices_pos;     /* Number of splices completed so far */
  size_t buffer_pos;        /* Number of samples through the current splice */
  size_t max_buffer_size;
  sox_sample_t * buffer;
  unsigned state;
} priv_t;

/**
 * SoX-dependent Functions
 *
 */

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
  size_t buffer_size = (MAX_PATH + wcslen(msg_template) + 20) * sizeof(WCHAR);
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
  sox_effect_t* splice_effect;
  sox_effects_chain_t * chain;
  sox_format_t** in_files;
  sox_format_t* out_file;
  char * * file_names = NULL;
  sox_sample_t samples[MAXIMUM_SAMPLES]; /* Temporary store while copying */
  size_t number_read;
  const char * current_filename;
  int sox_result = SOX_SUCCESS;
  int i;
  uint64_t first_file_ws; /* length of first file in samples */
  double first_file_secs; /* length of first file in seconds */

  // For testing...
  const int msg_length = 500;
  wchar_t status_msg[msg_length + 1];
  int filename_length;
  wchar_t *filenamebuf;
  // End of testing variables...

  file_names = malloc(sizeof(char *) * file_count);

  if (file_names == NULL)
  {
    report_error(NULL, ST_ERROR, __FILE__, __LINE__);
    cleanup();
    return;
  }

  for (i = 0; i < file_count; ++i)
  {
    current_filename = convert_pwstr_to_const_char(wfile_names[i]);
    if (current_filename == NULL)
    {
      report_error(NULL, ST_ERROR, __FILE__, __LINE__);
      cleanup();
      return;
    }
    file_names[i] = (char *)malloc((sizeof(char) * strlen(current_filename)) + 1);
    if (file_names[i] == NULL)
    {
      report_error(NULL, ST_ERROR, __FILE__, __LINE__);
      cleanup();
      return;
    }
    StringCbCopyA(file_names[i], strlen(current_filename) + 1, current_filename);
  }

  in_files = (sox_format_t**)lsx_malloc(file_count * sizeof(sox_format_t*));
  if (in_files == NULL)
  {
    report_error(NULL, SOX_LIB_ERROR, __FILE__, __LINE__);
    cleanup();
    return;
  }
  for (size_t n = 0; n < file_count; ++n)
  {
    in_files[n] = sox_open_read(file_names[n], &st_default_signalinfo, NULL, NULL);
    if (in_files[n] == NULL)
    {
      report_error(NULL, SOX_LIB_ERROR, __FILE__, __LINE__);
      for (size_t j = 0; j < n; ++j)
        sox_close(in_files[j]);
      free(in_files);
      cleanup();
      return;
    }
    filename_length = MultiByteToWideChar(CP_ACP, 0, in_files[n]->filename, -1, NULL, 0);
    filenamebuf = (PWSTR)CoTaskMemAlloc(filename_length * sizeof(WCHAR));
    MultiByteToWideChar(CP_ACP, 0, in_files[n]->filename, -1, filenamebuf, filename_length);
    uint64_t ws = in_files[n]->signal.length / max(in_files[n]->signal.channels, 1);
    double secs = (double)ws / max(in_files[n]->signal.rate, 1);

    StringCchPrintfW(status_msg, msg_length * sizeof(wchar_t), 
      L"FILE: %s | ENCODING: %d | SAMPLESIZE: %u | RATE: %g | CHANNELS: %u | RUNTIME: %s", 
      filenamebuf,
      in_files[n]->encoding.encoding,
      in_files[n]->encoding.bits_per_sample,
      in_files[n]->signal.rate,
      in_files[n]->signal.channels,
      str_time(secs)
    );
    report_current_action(NULL, convert_pwstr_to_const_char(status_msg));
    CoTaskMemFree(filenamebuf);
  }
  first_file_ws = in_files[0]->signal.length / max(in_files[0]->signal.channels, 1);
  first_file_secs = (double)first_file_ws / max(in_files[0]->signal.rate, 1);
  out_file = sox_open_write(DEFAULT_OUTPUT_FILENAME, &in_files[0]->signal, NULL, NULL, NULL, NULL);
  if (out_file == NULL)
  {
    report_error(NULL, SOX_LIB_ERROR, __FILE__, __LINE__);
    for (size_t x = 0; x < file_count; ++x)
      sox_close(in_files[x]);
    free(in_files);
    cleanup();
    return;
  }
  // status_msg[msg_length]

//  filename_length = MultiByteToWideChar(CP_ACP, 0, out_file->filename, -1, NULL, 0);
//  filenamebuf = (PWSTR)CoTaskMemAlloc(filename_length * sizeof(WCHAR));
//  MultiByteToWideChar(CP_ACP, 0, out_file->filename, -1, filenamebuf, filename_length);
//
//  StringCchPrintfW(status_msg, msg_length * sizeof(wchar_t), 
//    L"FILE: %s | ENCODING: %d | SAMPLESIZE: %u | RATE: %g | CHANNELS: %u", 
//    filenamebuf,
//    out_file->encoding.encoding,
//    out_file->encoding.bits_per_sample,
//    out_file->signal.rate,
//    out_file->signal.channels
//  );
//  report_current_action(NULL, convert_pwstr_to_const_char(status_msg));
//  CoTaskMemFree(filenamebuf);

  chain = sox_create_effects_chain(&in_files[0]->encoding, &out_file->encoding);
  report_current_action(NULL, "Now adding input effect to first file");
  sox_add_effect(chain, sox_create_effect(sox_find_effect("input")), &in_files[0]->signal, &in_files[0]->signal);
  splice_effect = sox_create_effect(sox_find_effect("splice"));
  /*  sox babayaga.wav greatgate.wav _merged.wav splice -q `soxi -D babayaga.wav`,0.1 */
  char file_count_str[10];
  snprintf(file_count_str, sizeof(file_count_str), "%d", file_count);
  char* splice_args[] = { "-q", str_time(first_file_secs), DEFAULT_SPLICE_OVERLAP, NULL };
  report_current_action(NULL, "Now adding splice options");
  int args_added = sox_effect_options(splice_effect, 3, splice_args);
  report_current_action(NULL, "Now adding splice effect to first input file");
  sox_add_effect(chain, splice_effect, &in_files[0]->signal, &in_files[0]->signal);
  report_current_action(NULL, "Now adding output effect to last file");
  sox_add_effect(chain, sox_create_effect(sox_find_effect("output")), &in_files[file_count-1]->signal, &out_file->signal);
  report_current_action(NULL, "About to flow");
  int result = sox_flow_effects(chain, NULL, NULL);
  if (result != SOX_SUCCESS)
  {
    const char* error_message = sox_strerror(result);
    report_current_action(NULL, error_message);
    report_error(NULL, result, __FILE__, __LINE__);
  }
  report_current_action(NULL, "It flowed");
  sox_delete_effects_chain(chain);
  for (size_t i = 0; i < file_count; ++i)
    sox_close(in_files[i]);
  free(in_files);
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

