/* wt.c
 *
 * (c) 2023 Michael Toulouse
 *
 * Main entry point for the wt application.
 *
 */

#define WINVER 0x0600
#define _WIN32_WINNT 0x0600
#include "sox.h"
#include "wt.h"
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

static char *starting_directory[sizeof(TCHAR) * MAX_PATH + 1];
static char *working_directory[sizeof(TCHAR) * MAX_PATH + 1];
char ** filenames;

/**
 * General Utilities
 *
 */
int compare_filenames(const void* a, const void* b)
{
  size_t digits_to_compare = DEFAULT_TRACK_NUMER_WIDTH;
  return strncmp(*(const char**)a, *(const char**)b, digits_to_compare);
}

int ends_with(const TCHAR *str, const TCHAR *suffix)
{
  if (!str || !suffix)
    return 0;
  size_t lenstr = wcslen(str);
  size_t lensuffix = wcslen(suffix);
  if (lensuffix > lenstr)
    return 0;
  return wcsncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

int is_wav_file(const TCHAR *str)
{
  if (ends_with(str, L".wav") || ends_with(str, L".WAV"))
    return 1;
  return 0;
}

const char* convert_pwstr_to_const_char(PWSTR wideString)
{
  int length = WideCharToMultiByte(CP_UTF8, 0, wideString, -1, NULL, 0, NULL, NULL);
  char* buffer = (char*)malloc(length * sizeof(char) + 1);
  if (buffer == NULL)
  {
    report_error(NULL, ST_ERROR, __FILE__, __LINE__);
    cleanup();
    exit(1);
  }
  if (WideCharToMultiByte(CP_UTF8, 0, wideString, -1, buffer, length, NULL, NULL) == 0)
  {
    report_error(NULL, ST_ERROR, __FILE__, __LINE__);
    free(buffer);
    cleanup();
    exit(1);
  }
  return buffer;
}

size_t count_files()
{
  size_t count = 0;
  char **current = filenames;

  while (*current != NULL)
  {
    ++count;
    ++current;
  }
  return count;
}

void load_filenames(PWSTR directory_path)
{
  STRSAFE_LPSTR wav_wildcard = L"*.wav";
  WIN32_FIND_DATA fdFile;
  HANDLE hFind = NULL;
  filenames = (char**)CoTaskMemAlloc(1 * sizeof(char*));

  int file_count = 0;
  sox_format_t * current_sound_file;
  PWSTR buffer = NULL;
  if((hFind = FindFirstFile(wav_wildcard, &fdFile)) != INVALID_HANDLE_VALUE)
  {
    do {
      /* FindFirstFile will always return "." and ".."
       * as the first two directories. */
      if(strcmp(fdFile.cFileName, ".") != 0
          && strcmp(fdFile.cFileName, "..") != 0)
      {
        file_count++;
        filenames = (char**)CoTaskMemRealloc(filenames, file_count * sizeof(char *));
        filenames[file_count - 1] = (char *)malloc(wcslen(fdFile.cFileName) + 1);
        StringCbPrintfA(filenames[file_count - 1], (wcslen(fdFile.cFileName) + 1), 
          "%s", convert_pwstr_to_const_char(fdFile.cFileName));
      }
    }
    while(FindNextFile(hFind, &fdFile));
    FindClose(hFind);
  }

  size_t buffer_size = (wcslen(L"File Count: ") + 10) * sizeof(WCHAR);
  PWSTR msgbuf = (PWSTR)CoTaskMemAlloc(buffer_size);
  StringCbPrintfW(msgbuf, buffer_size, L"File Count: %d", file_count);
  MessageBox(NULL, msgbuf, L"FILES SELECTED", MB_OK);
  CoTaskMemFree(msgbuf);

  qsort(filenames, file_count, sizeof(char *), compare_filenames);

  /* Add a final null terminator so we can calculate the number of files */
  filenames[file_count] = NULL;
}

/**
 * Windows functions and variables
 *
 */

 /* Global variables */
HMENU hMenu;
IFileDialog* pFileOpenDialog;
#define APP_WINDOW_HEIGHT         350
#define APP_WINDOW_WIDTH          450
#define TEXT_MARGIN_VERTICAL      10
#define TEXT_MARGIN_HORIZONTAL    10
#define IDM_FILE_OPEN             1
#define IDM_FILE_EXIT             3

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

void report_error(HWND hwnd, int errcode, char * file, int line_number)
{
  PWSTR filenamebuf;

  TCHAR *msg_template = L"ERROR %d at line %d in %s\n";
  int filename_length = MultiByteToWideChar(CP_ACP, 0, file, -1, NULL, 0);
  filenamebuf = (PWSTR)CoTaskMemAlloc(filename_length * sizeof(WCHAR));
  MultiByteToWideChar(CP_ACP, 0, file, -1, filenamebuf, filename_length);
  size_t buffer_size = (wcslen(msg_template) + 20 + filename_length) * sizeof(WCHAR);
  PWSTR msgbuf = (PWSTR)CoTaskMemAlloc(buffer_size);
  StringCbPrintfW(msgbuf, buffer_size, msg_template, errcode, line_number, filenamebuf);
  MessageBox(hwnd, msgbuf, L"ERROR", MB_OK);
  CoTaskMemFree(filenamebuf);
  CoTaskMemFree(msgbuf);
}

void report_current_action(HWND hwnd, const char* message)
{
  PWSTR msgbuf;
  int message_length = MultiByteToWideChar(CP_ACP, 0, message, -1, NULL, 0);
  msgbuf = (PWSTR)CoTaskMemAlloc((message_length * sizeof(WCHAR) + 1));
  MultiByteToWideChar(CP_ACP, 0, message, -1, msgbuf, message_length);
  MessageBox(hwnd, msgbuf, L"CURRENT ACTION", MB_OK);
  CoTaskMemFree(msgbuf);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

/* Add up duration of the audio files using SoX */
DWORD WINAPI DurationThreadProc()
{
  int const message_length = 200;
  TCHAR message[message_length];
  size_t cb_dest = message_length * sizeof(TCHAR);
  TCHAR *msg_template = TEXT("TOTAL DURATION ... %s\n");
  int result;

  load_filenames(working_directory);
  if (filenames != NULL)
  {
    result = total_duration();
    StringCbPrintf(message, cb_dest, msg_template, str_time(result));
    MessageBox(NULL, message, L"RESULT", MB_OK);
  }
  CoTaskMemFree(filenames);
  return 0;
}

#define NOMINMAX // from example on stackoverflow.com

int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
  const TCHAR CLASS_NAME[] = L"Audio File Timing";

  filenames = NULL;

  WNDCLASS wc = { };

  wc.lpfnWndProc    = WindowProc;
  wc.hInstance      = hInstance;
  wc.lpszClassName  = CLASS_NAME;
  wc.style          = CS_VREDRAW | CS_HREDRAW;

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
    L"Audio Timing",        // Window text
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
        report_error(hwnd, sox_result, __FILE__, __LINE__);
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
            GetCurrentDirectory((sizeof(TCHAR) * MAX_PATH), starting_directory);
            hr = pFileOpenDialog->lpVtbl->Show(pFileOpenDialog, hwnd);
            if (SUCCEEDED(hr))
            {
              IShellItem* pSelectedItem = NULL;
              TCHAR *pszFolderPath = NULL;
              hr = pFileOpenDialog->lpVtbl->GetResult(pFileOpenDialog, &pSelectedItem);
              if (SUCCEEDED(hr))
              {
                hr = pSelectedItem->lpVtbl->GetDisplayName(pSelectedItem, SIGDN_FILESYSPATH, &pszFolderPath);
                if (SUCCEEDED(hr))
                {
                  int filePathLength = (wcslen(pszFolderPath) + 1) * sizeof(TCHAR);
                  wcscpy_s(working_directory, filePathLength, pszFolderPath);
                  if (filePathLength < MAX_PATH)
                  {
                    SetCurrentDirectory(working_directory);
                    DWORD dwThreadId;
                    set_wait_cursor();
                    HANDLE hThread = CreateThread(NULL, 0, DurationThreadProc, NULL, 0, &dwThreadId);
                    if (hThread != NULL)
                    {
                      CloseHandle(hThread);
                    } else {
                      report_error(hwnd, ST_ERROR, __FILE__, __LINE__);
                    }
                    restore_cursor();
                    CoTaskMemFree(pszFolderPath);
                  } else {
                    report_error(hwnd, filePathLength, __FILE__, __LINE__);
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
      SetCurrentDirectory(starting_directory);
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
      HFONT hf;
      long lfHeight;
      RECT rect;
      HFONT g_hfFont = GetStockObject(DEFAULT_GUI_FONT);
      // Calculate the number of pixels required for a 12-point font.
      // MulDiv is a legacy function that simulates floating-point calculations
      // using integers.
      lfHeight = -MulDiv(12, GetDeviceCaps(hdc, LOGPIXELSY), 72);
      hf = CreateFont(lfHeight, 0, 0, 0, 0, FALSE, 0, 0, 0, 0, 0, 0, 0, L"Calibri");
      if (hf)
      {
        DeleteObject(g_hfFont);
        g_hfFont = hf;
      }
      else
      {
        MessageBox(hwnd, L"Font creation failed!", L"Error", MB_OK | MB_ICONEXCLAMATION);
      }
      // All painting occurs here, between BeginPaint and EndPaint.
      FillRect(hdc, &ps.rcPaint, (HBRUSH) (COLOR_WINDOW+1));
      HFONT hfOld = SelectObject(hdc, hf);
      SetTextAlign(hdc, TA_TOP | TA_LEFT);
      GetClientRect(hwnd, &rect);
      InflateRect(&rect, -TEXT_MARGIN_HORIZONTAL, -TEXT_MARGIN_VERTICAL);
      DrawTextEx(hdc,
        L"WAV TIMER\n\nThis application calculates the total duration of all .wav audio files in a directory. "\
          "It can handle up to fifty files. The directory cannot contain anything but .wav files.\n\n"\
          "To get started, click 'Folder | Select' on the menu above.",
        -1, &rect,
        DT_EDITCONTROL | DT_WORDBREAK,
        NULL
      );
      SelectObject(hdc, hfOld);
      EndPaint(hwnd, &ps);
    }
    return 0;
    default:
      return DefWindowProc(hwnd, uMsg, wParam, lParam);
  }
}

