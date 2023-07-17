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

TCHAR const * str_time(double seconds)
{
  static TCHAR string[16][50];
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
    StringCchPrintfW(string[i], cchDest * sizeof(TCHAR), pszFormatWithHours, hours, mins, seconds);
  } else {
    StringCchPrintfW(string[i], cchDest * sizeof(TCHAR), pszFormat, mins, seconds);
  }
  return string[i];
}

void show_name_and_runtime(sox_format_t * in)
{
  double secs;
  uint64_t ws;
  PWSTR msgbuf, filenamebuf;

  TCHAR *msg_template = L"%s ... %-15.15s\n";
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

void trim_silence(TCHAR * filename, char * duration, char * threshold)
{
  TCHAR szNewPath[MAX_PATH * sizeof(TCHAR)];
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
void splice()
{
  sox_format_t * output = NULL;
  size_t i, sox_result;
  for (i = 0; i < count_files(); ++i)
  {
    report_current_action(NULL, filenames[i]);
  }

  for (i = 0; i < count_files(); ++i)
  {
    sox_format_t * input;
    static sox_signalinfo_t signal; /* static quashes 'uninitialized' warning. */

    /* The (maximum) number of samples that we shall read/write at a time;
     * chosen as a rough match to typical operating system I/O buffer size: */
    #define MAX_SAMPLES (size_t)2048
    sox_sample_t samples[MAX_SAMPLES]; /* Temporary store whilst copying */
    size_t number_read, number_written;

    /* Open this input file: */

    input = sox_open_read(filenames[i], NULL, NULL, NULL);
    if (input == NULL)
    {
      report_error(NULL, ST_ERROR, __FILE__, __LINE__);
      cleanup();
      return;
    }
    if (i == 0) /* If this is the first input file... */
    {
      /* report_current_action(NULL, "First file"); */
      /* Open the output file using the same signal and encoding character-
       * istics as the first input file.  Note that here, input->signal.length
       * will not be equal to the output file length so we are relying on
       * libSoX to set the output length correctly (i.e. non-seekable output
       * is not catered for) */
      output = sox_open_write(DEFAULT_OUTPUT_FILENAME, 
        &input->signal, &input->encoding, NULL, NULL, NULL);
      if (output == NULL)
      {
        report_error(NULL, ST_ERROR, __FILE__, __LINE__);
        cleanup();
        return;
      }
      /* Also, we'll store the signal characteristics of the first file
       * so that we can check that these match those of the other inputs: */
      signal = input->signal;
    } else { /* Second or subsequent input file... */
      /* report_current_action(NULL, "Second file"); */
      /* Check that this input file's signal matches that of the first file: */
      if ((input->signal.channels != signal.channels) ||
                          (input->signal.rate != signal.rate))
      {
        report_error(NULL, ST_ERROR, __FILE__, __LINE__);
        cleanup();
        return;
      }
    }
    /* Copy all of the audio from this input file to the output file: */
    while ((number_read = sox_read(input, samples, MAX_SAMPLES)))
    {
      number_written = sox_write(output, samples, number_read);
      if(number_written != number_read)
      {
        report_error(NULL, ST_ERROR, __FILE__, __LINE__);
        cleanup();
        return;
      }
    }
    sox_result = sox_close(input);
    if(sox_result != SOX_SUCCESS)
    {
      report_error(NULL, ST_ERROR, __FILE__, __LINE__);
      cleanup();
      return;
    }
  }
  sox_result = sox_close(output);
  if(sox_result != SOX_SUCCESS)
  {
    report_error(NULL, ST_ERROR, __FILE__, __LINE__);
    cleanup();
    return;
  }
  output = NULL;
}

/* All done; tidy up... */
int cleanup()
{
  STRSAFE_LPSTR sox_wildcard = L"libSoX.tmp*";
  TCHAR szTempFileWildcard[MAX_PATH * sizeof(TCHAR)];
  TCHAR szCurrentTempFileName[MAX_PATH * sizeof(TCHAR)];
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

  if (filenames != NULL)
  {
    while (*filenames != NULL)
    {
      CoTaskMemFree(*filenames);
      ++filenames;
    }
    CoTaskMemFree(filenames);
  }
  return 0;
}

