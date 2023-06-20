/* Splice
 *
 * (c) 2023 Michael Toulouse
 *
 */

#define WINVER 0x0600
#define _WIN32_WINNT 0x0600
#include "sox.h"
#include "splice.h"
#include <stdlib.h>
#include <errno.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <objbase.h>
#include <string.h>
#include <strsafe.h>
#include <string.h>
#include <wchar.h>
#include <dirent.h>
#include <stdio.h>
#include <assert.h>

/**
 * General Utilities
 *
 */
int compare_filenames(const void* a, const void* b)
{
  return strcmp(*(const char**)a, *(const char**)b);
}

int ends_with(const wchar_t *str, const wchar_t *suffix)
{
  if (!str || !suffix)
    return 0;
  size_t lenstr = wcslen(str);
  size_t lensuffix = wcslen(suffix);
  if (lensuffix > lenstr)
    return 0;
  return wcsncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

int is_wav_file(const wchar_t *str)
{
  if (ends_with(str, L".wav") || ends_with(str, L".WAV"))
    return 1;
  return 0;
}

const char* ConvertPWSTRToConstChar(PWSTR wideString)
{
  int length = WideCharToMultiByte(CP_UTF8, 0, wideString, -1, NULL, 0, NULL, NULL);
  char* buffer = (char*)malloc(length * sizeof(char));
  if (buffer == NULL)
  {
    report_error(NULL, -1, __LINE__);
    cleanup();
    exit(1);
  }
  if (WideCharToMultiByte(CP_UTF8, 0, wideString, -1, buffer, length, NULL, NULL) == 0)
  {
    report_error(NULL, -1, __LINE__);
    free(buffer);
    cleanup();
    exit(1);
  }
  return buffer;
}

int count_files(wchar_t** filenames)
{
  size_t count = 0;
  while (*filenames != NULL)
  {
    ++count;
    ++filenames;
  }
  return count;
}

wchar_t** sort_files(PWSTR directory_path)
{
  DIR* directory;
  struct _wdirent* entry;
  wchar_t** filenames;
  int file_count = 0;
  sox_format_t * current_sound_file;
  PWSTR buffer;

  directory = _wopendir(directory_path);
  if (directory == NULL)
  {
    size_t dirPathLength = wcslen(directory_path);
    buffer = (PWSTR)CoTaskMemAlloc((dirPathLength + 1) * sizeof(WCHAR));
    wcscpy_s(buffer, sizeof(buffer), directory_path);
    report_error(NULL, -1, __LINE__);
    CoTaskMemFree(buffer);
    return NULL;
  }

  while ((entry = _wreaddir(directory)) != NULL)
  {
    const wchar_t* file_name = entry->d_name;
    if (is_wav_file(file_name))
    {
      wchar_t file_path[MAX_PATH * sizeof(WCHAR) + 1];
      StringCbPrintfW(file_path, (MAX_PATH * sizeof(WCHAR)), L"%s\\%s", directory_path, file_name);
      DWORD file_attributes = GetFileAttributes(file_path);
      if (file_attributes == 0)
      {
        MessageBox(NULL, L"Invalid file_attributes", L"ERROR", MB_OK);
      }
      if (file_attributes == -1)
      {
        MessageBox(NULL, L"File not found", L"ERROR", MB_OK);
      }
      if (file_attributes != INVALID_FILE_ATTRIBUTES &&
          !(file_attributes & FILE_ATTRIBUTE_DIRECTORY))
      {
        file_count++;
      }
    }
  }

  // size_t buffer_size = (wcslen(L"File Count: ") + 10) * sizeof(WCHAR);
  // PWSTR msgbuf = (PWSTR)CoTaskMemAlloc(buffer_size);
  // StringCbPrintfW(msgbuf, buffer_size, L"File Count: %d", file_count);
  // MessageBox(NULL, msgbuf, L"SANITY CHECK", MB_OK);
  // CoTaskMemFree(msgbuf);

  filenames = (wchar_t**)CoTaskMemAlloc(file_count * sizeof(wchar_t));

  _wrewinddir(directory);

  int i = 0;
  while((entry = _wreaddir(directory)) != NULL)
  {
    const wchar_t* file_name = entry->d_name;
    if (is_wav_file(file_name))
    {
      wchar_t file_path[MAX_PATH * sizeof(WCHAR) + 1];
      StringCbPrintfW(file_path, (MAX_PATH * sizeof(WCHAR)), L"%s\\%s", directory_path, file_name);
      DWORD file_attributes = GetFileAttributes(file_path);
      if (file_attributes != INVALID_FILE_ATTRIBUTES &&
          !(file_attributes & FILE_ATTRIBUTE_DIRECTORY))
      {
        filenames[i] = wcsdup(entry->d_name);
        i++;
      }
    }
  }

  qsort(filenames, file_count, sizeof(wchar_t *), compare_filenames);

  /* Add a final null terminator so we can calculate the number of files */
  filenames[i] = NULL;
  free(buffer);
  closedir(directory);
  return filenames;
}

/**
 * Windows functions and variables
 *
 */

 /* Global variables */
HMENU hMenu;
IFileDialog* pFileOpenDialog;
#define APP_WINDOW_HEIGHT   350
#define APP_WINDOW_WIDTH    450
#define IDM_FILE_OPEN       1
#define IDM_FILE_EXIT       3

HCURSOR original_cursor;

void set_wait_cursor()
{
  HCURSOR wait_cursor = LoadCursor(NULL, IDC_WAIT);
  original_cursor = SetCursor(wait_cursor);
}

void restore_cursor()
{
  SetCursor(original_cursor);
}

void report_error(HWND hwnd, int errcode, int line_number)
{
  wchar_t *msg_template = L"ERROR %d at line %d in %s\nError Number: %d\n";
  size_t buffer_size = (wcslen(msg_template) + 20) * sizeof(WCHAR);
  PWSTR msgbuf = (PWSTR)CoTaskMemAlloc(buffer_size);
  StringCbPrintfW(msgbuf, buffer_size, msg_template, errcode, line_number);
  MessageBox(hwnd, msgbuf, L"ERROR", MB_OK);
  CoTaskMemFree(msgbuf);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

PWSTR              pszWorkingDirectory;

/* Splice the audio files using SoX */
DWORD WINAPI SpliceThreadProc()
{
  wchar_t**       soundfiles;
  int             index;

  soundfiles = sort_files(pszWorkingDirectory);
  if (soundfiles != NULL)
  {
    // Trim silence from beginning and end of each file
    for (index = 0; index < count_files(soundfiles); index++)
    {
      // trim_silence(soundfiles[index], DEFAULT_NOISE_DURATION, DEFAULT_SILENCE_THRESHOLD);
    }
    // Concatenate and splice the files
    splice(soundfiles);
    CoTaskMemFree(soundfiles);
  }
  return 0;
}

#define NOMINMAX // from example on stackoverflow.com

int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
  const wchar_t CLASS_NAME[] = L"Splicing Audio Files";

  WNDCLASS wc = { };

  wc.lpfnWndProc    = WindowProc;
  wc.hInstance      = hInstance;
  wc.lpszClassName  = CLASS_NAME;

  RegisterClass(&wc);

  // Initialize COM library
  HRESULT hr = CoInitialize(NULL);
  if (FAILED(hr))
  {
      // Handle COM initialization error
      return -1;
  }

  HWND hwnd = CreateWindowEx(
    0,                      // Optional window styles
    CLASS_NAME,             // Window class
    L"Splicing Audio",      // Window text
    WS_OVERLAPPEDWINDOW,    // Window style
    // Size and position
    CW_USEDEFAULT, CW_USEDEFAULT, APP_WINDOW_WIDTH, APP_WINDOW_HEIGHT,

    NULL,                   // Parent window
    hMenu,                  // Menu
    hInstance,              // Instance handle
    NULL                    // Additional application data
  );

  if (hwnd == NULL)
  {
    return 0;
  }

  hMenu = CreateMenu();
  HMENU hFileMenu = CreateMenu();

  AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFileMenu, L"Folder");
  AppendMenu(hFileMenu, MF_STRING, IDM_FILE_OPEN, L"Select");
  AppendMenu(hFileMenu, MF_STRING, IDM_FILE_EXIT, L"Exit");

  SetMenu(hwnd, hMenu);
  ShowWindow(hwnd, nCmdShow);
  UpdateWindow(hwnd);
  pFileOpenDialog = NULL;

  // Run the message loop.
  MSG msg = { };
  while(GetMessage(&msg, NULL, 0, 0) > 0)
  {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  // Uninitialize COM library
  CoUninitialize();
  return msg.wParam;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch(uMsg)
  {
  case WM_CREATE:
    {
      int sox_result;
      DWORD dwRet;

      CREATESTRUCT *pCreate = (CREATESTRUCT*)(lParam);
      sox_result = sox_init();
      sox_quit_called = 0;
      if (sox_result != SOX_SUCCESS)
      {
        report_error(hwnd, sox_result, __LINE__);
        cleanup();
      }
      return sox_result;
    }
  case WM_COMMAND:
    switch(LOWORD(wParam))
    {
      case IDM_FILE_OPEN:
        {
          HRESULT hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_ALL, &IID_IFileDialog, (void**)&pFileOpenDialog);
          if (SUCCEEDED(hr))
          {
            DWORD dwOptions;
            pFileOpenDialog->lpVtbl->GetOptions(pFileOpenDialog, &dwOptions);
            pFileOpenDialog->lpVtbl->SetOptions(pFileOpenDialog, dwOptions | FOS_PICKFOLDERS | FOS_FILEMUSTEXIST);
            pFileOpenDialog->lpVtbl->SetOkButtonLabel(pFileOpenDialog, L"Select Folder");

            hr = pFileOpenDialog->lpVtbl->Show(pFileOpenDialog, hwnd);
            if (SUCCEEDED(hr))
            {
              IShellItem* pSelectedItem = NULL;
              hr = pFileOpenDialog->lpVtbl->GetResult(pFileOpenDialog, &pSelectedItem);
              if (SUCCEEDED(hr))
              {
                PWSTR pszFolderPath = NULL;
                hr = pSelectedItem->lpVtbl->GetDisplayName(pSelectedItem, SIGDN_FILESYSPATH, &pszFolderPath);
                size_t filePathLength = wcslen(pszFolderPath);
                pszWorkingDirectory = (PWSTR)CoTaskMemAlloc((filePathLength + 1) * sizeof(WCHAR));
                if (pszWorkingDirectory != NULL)
                {
                  wcscpy_s(pszWorkingDirectory, filePathLength + 1, pszFolderPath);
                  if (filePathLength < MAX_PATH)
                  {
                    SetCurrentDirectory(pszWorkingDirectory);
                    DWORD dwThreadId;
                    set_wait_cursor();
                    HANDLE hThread = CreateThread(NULL, 0, SpliceThreadProc, NULL, 0, &dwThreadId);
                    if (hThread != NULL)
                    {
                      CloseHandle(hThread);
                    } else {
                      report_error(hwnd, -1, __LINE__);
                    }
                    restore_cursor();
                  } else {
                    report_error(hwnd, filePathLength, __LINE__);
                  }
                }
                pSelectedItem->lpVtbl->Release(pSelectedItem);
              }
            }
            pFileOpenDialog->lpVtbl->Release(pFileOpenDialog);
            pFileOpenDialog = NULL;
          }
        }
        break;
      case IDM_FILE_EXIT:
        DestroyWindow(hwnd);
        break;
    }
    break;
  case WM_CLOSE:
    {
      DestroyWindow(hwnd);
    }
    break;
  case WM_DESTROY:
    {
      cleanup();
      PostQuitMessage(0);
    }
    break;
  case WM_PAINT:
    {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT rect;

      // All painting occurs here, between BeginPaint and EndPaint.
      FillRect(hdc, &ps.rcPaint, (HBRUSH) (COLOR_WINDOW+1));
      GetClientRect(hwnd, &rect);
      DrawTextEx(hdc,
        L"FILE SPLICER\n\nThis application splices all the .wav audio files in a directory. "\
          "The ordering of the files' contents in the output is determined by "\
          "the names of the files, so please make sure each filename starts with the correct track number. "\
          "Leading zeroes will be ignored. You can splice up to fifty files in a single directory.\n\n"\
          "The output file (spliced-audio.wav) will be placed in the same folder as the input files.\n\n"\
          "To get started, click 'Folder | Select' on the menu above.",
        -1, &rect,
        DT_CENTER | DT_EDITCONTROL | DT_WORDBREAK,
        NULL
      );
      EndPaint(hwnd, &ps);
    }
    return 0;
  }
  return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

