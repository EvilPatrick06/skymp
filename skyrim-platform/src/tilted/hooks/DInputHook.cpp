#include "IInputListener.h"
#include <DInputHook.hpp>

#define CINTERFACE

#include <dinput.h>

#include <FunctionHook.hpp>
#include <array>
#include <cstdio>
#include <iostream>

namespace {
std::shared_ptr<IInputListener> g_listener;
std::array<uint8_t, 256> g_pressedWas = ([] {
  std::array<uint8_t, 256> r;
  r.fill(0);
  return r;
})();
std::array<bool, 4> g_mousePressedWas = { 0, 0, 0, 0 };

/*
  THORNSWOOD PATCH. A meter on the input path.

  Four explanations for the keyboard dying have now been built out of the
  client log, and the client log cannot see the thing that matters. It reports
  what reached the plugin. When nothing reaches the plugin every explanation
  fits equally well, so what is needed is the state of the device itself: was
  the hook even called, what did the read return, and who if anyone threw the
  keys away on purpose.

  One line every five seconds into Data\Platform\thornswood-input.log, which
  sits beside the plugin logs. It is a buffered write on a thread that already
  writes thousands of spdlog lines a second, and the counters are plain adds.
  Nothing is asked of another thread and nothing is asked of Windows except
  GetTickCount64 and IsIconic, both of which return from a page of shared
  memory. The line written across the five seconds where no key arrives is the
  answer, and the timestamps close the last hole: no line at all means the game
  stopped polling, which is a different bug entirely from a device that is gone.
*/
struct InputMeter
{
  unsigned long long dataCalls, stateCalls, kbdReads;
  unsigned long long lostData, lostState, lostKbd;
  unsigned long long chromeAte, minimisedAte, keyChanges;
  long lastData, lastState, lastKbd;
};
static InputMeter g_meter = {};

static FILE* InputLog()
{
  static FILE* f = nullptr;
  static bool tried = false;
  if (!tried) {
    tried = true;
    fopen_s(&f, "Data\\Platform\\thornswood-input.log", "w");
  }
  return f;
}

/*
  THORNSWOOD PATCH, third pass. Who else is on the keyboard.

  The meter ruled out everything that had been guessed. Through the lock the
  hook is called two hundred and fifty times every five seconds, every read
  returns DI_OK, not one comes back INPUTLOST or NOTACQUIRED, nothing is being
  thrown away on purpose, the game is not in a menu and the controls are on.
  Our own full read of the keyboard succeeds and says no key is down, while
  keys are being pressed.

  DirectInput does that in one situation: somebody else holds the device
  EXCLUSIVE. Every other client then gets success and an empty buffer. It also
  explains the shape this had before any of these patches, when the game asked
  for the keyboard FOREGROUND and was simply revoked instead.

  Every device in this process is created through this hook, so every
  SetCooperativeLevel lands here and the caller cannot hide. One line per call
  with the device's own name and the flags it asked for names whoever it is.
*/
static void LogCoop(const char* who, DWORD asked, DWORD used)
{
  FILE* f = InputLog();
  if (!f)
    return;
  fprintf(f, "[coop] %-40s asked 0x%08lX%s%s%s%s  ->  used 0x%08lX\n", who,
          asked, (asked & DISCL_EXCLUSIVE) ? " EXCLUSIVE" : "",
          (asked & DISCL_NONEXCLUSIVE) ? " nonexclusive" : "",
          (asked & DISCL_FOREGROUND) ? " foreground" : "",
          (asked & DISCL_BACKGROUND) ? " background" : "", used);
  fflush(f);
}

static void ReportInput(bool chromeFocus, bool readable, void* gameWindow)
{
  static unsigned long long last = 0;

  unsigned long long now = GetTickCount64();
  if (last == 0) {
    last = now;
    return;
  }
  if (now - last < 5000)
    return;
  unsigned long long span = now - last;
  last = now;

  FILE* f = InputLog();
  if (!f)
    return;

  fprintf(f,
          "[input] +%llums  getData %llu (lost %llu, last 0x%08lX)  "
          "getState %llu (lost %llu, last 0x%08lX)  "
          "kbdRead %llu (lost %llu, last 0x%08lX)  keys %llu  "
          "chromeAte %llu  minimisedAte %llu  chromeFocus %s  readable %s  "
          "window %p  focus %p  active %p  fg %p\n",
          span, g_meter.dataCalls, g_meter.lostData, g_meter.lastData,
          g_meter.stateCalls, g_meter.lostState, g_meter.lastState,
          g_meter.kbdReads, g_meter.lostKbd, g_meter.lastKbd,
          g_meter.keyChanges, g_meter.chromeAte, g_meter.minimisedAte,
          chromeFocus ? "yes" : "no", readable ? "yes" : "no", gameWindow,
          (void*)GetFocus(), (void*)GetActiveWindow(),
          (void*)GetForegroundWindow());
  fflush(f);

  InputMeter cleared = {};
  g_meter = cleared;
}

void ProcessKeyboardData(uint8_t* apData)
{
  if (!g_listener)
    return;

  for (uint32_t idx = 0; idx < 256; idx++) {
    if (g_pressedWas[idx] != apData[idx]) {
      g_pressedWas[idx] = apData[idx];
      g_meter.keyChanges++;
      g_listener->OnKeyStateChange(idx, apData[idx] != 0);
    }
  }
}

void ProcessMouseData(DIMOUSESTATE2* apMouseState)
{
  if (!g_listener)
    return;

  /*if (!g_listener->OnMouseMove()) {
    apMouseState->lX = apMouseState->lY = apMouseState->lZ = 0;
  }*/
  if (abs(apMouseState->lX) >= std::numeric_limits<float>::epsilon() ||
      abs(apMouseState->lY) >= std::numeric_limits<float>::epsilon())
    g_listener->OnMouseMove(apMouseState->lX, apMouseState->lY);

  if (apMouseState->lZ != 0) {
    g_listener->OnMouseWheel(apMouseState->lZ);
    if (CEFUtils::DInputHook::ChromeFocus()) {
      apMouseState->lZ = 0;
    }
  }

  static const IInputListener::MouseButton mouseBtns[] = {
    IInputListener::MouseButton::Left, IInputListener::MouseButton::Right,
    IInputListener::MouseButton::Middle
  };
  for (int i = 0; i < std::size(mouseBtns); ++i) {
    uint8_t& state = apMouseState->rgbButtons[i];
    const bool pressed = state & 0x80;
    if (pressed != g_mousePressedWas[i]) {
      g_mousePressedWas[i] = pressed;
      g_listener->OnMouseStateChange(mouseBtns[i], pressed);
    }
  }
}

}

namespace CEFUtils {

/*
  Asking for the device back when Windows has taken it away.

  This is the one change in this file and it is the documented handling for two
  specific errors, nothing more.

  DirectInput devices that a game acquires for the foreground are handed back
  the moment that window stops being the foreground one, and they are not
  returned on their own. Every read after that fails with DIERR_INPUTLOST or
  DIERR_NOTACQUIRED, forever, and this hook had nothing that noticed. The game
  goes deaf, and so does every plugin, because the snapshot below is read
  through the same device.

  Something takes the foreground for an instant when the microphone opens. The
  log is unambiguous about the shape of it:

      input: 323 keys in 5s, browser focus no, menu no, controls on
      input:   0 keys in 5s, browser focus no, menu no, controls on
      input:   0 keys in 5s, browser focus no, menu no, controls on

  No menu, nothing holding browser focus, player controls on, and not one key
  arriving anywhere in the process. That is not the game ignoring input. That is
  the device being gone. Alt tabbing back sometimes fixed it, which is the same
  story from the other end: something re acquired.

  So a lost read acquires once and reads again. It costs one extra call on a
  path that only runs when the device was already dead, and it makes the hook
  self healing whatever took the foreground and why.
*/
static bool Lost(HRESULT hr)
{
  return hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED;
}

/*
  THORNSWOOD PATCH. Stop the keyboard being taken away in the first place.

  The retry above heals a lost device, but it can only heal it once the game is
  the foreground window again. While something else holds the foreground every
  Acquire fails, and that is the twenty seconds to a minute of dead keyboard
  that shows up the moment proximity voice opens the microphone: the device is
  handed back to Windows and will not come back until the foreground does.

  A DirectInput device is only taken away on a foreground change if it was
  asked for with DISCL_FOREGROUND. The game asks for that, and for the mouse it
  is the right thing: exclusive foreground is what confines the cursor and
  drives the camera. For the keyboard it buys nothing. So the keyboard is
  asked for as DISCL_BACKGROUND | DISCL_NONEXCLUSIVE instead, which Windows
  never revokes. Whether the reads are then gated, and on what, is the note
  below: the first answer was wrong and the second one is the fix.

  Every Win32 call reached for here is a kernel call. None of them send a
  message to another thread and none of them can block, which EnumWindows and
  GetWindowText both can and both did, twice, from this thread.
*/
static HWND g_gameWindow = nullptr;

/*
  THORNSWOOD PATCH, second pass. Do not gate on the foreground.

  The first pass took the device off DISCL_FOREGROUND so Windows would stop
  revoking it, and then gated the reads on GetForegroundWindow so the game
  would still ignore input while genuinely alt tabbed. The keyboard died in
  exactly the same way, at exactly the same moment, for exactly as long.

  That is worth more than a fix would have been, because it settles what was
  never known. If SetCooperativeLevel had not been reaching this hook, the
  gate could not have blocked anything: g_gameWindow would still be null and
  the check returns true. It blocked. So the hook is on the path, the device
  is no longer being taken away, and the thing that was actually happening all
  along is the plain one: when voice opens the microphone the foreground
  leaves this game and stays away for twenty seconds to a minute. Every
  Acquire failing was the symptom of that, not the cause.

  Something holding the foreground is not a reason to throw the keys away.
  The game is still on screen and the person is still looking at it, so the
  keys are passed through. The only case that is really somebody having left
  is the window being minimised, and IsIconic answers that without asking who
  has focus, which is the question that cannot be answered here without
  reproducing the bug.
*/
static bool ShouldReadInput()
{
  if (!g_gameWindow)
    return true; // never saw SetCooperativeLevel: behave exactly as stock

  if (IsIconic(g_gameWindow))
    return false;

  /*
    THORNSWOOD PATCH, fifth pass. The foreground gate is back, and this time
    for the right reason.

    It came out earlier because the keyboard was dying and this looked like the
    thing throwing the keys away. The meter has since settled that it was not:
    through every lock chromeAte and minimisedAte are zero, nothing here ever
    discarded a key, DirectInput returned DI_OK on every read, and the game was
    receiving input and ignoring it. So taking the gate out fixed nothing and
    cost something real, which people then reported in two different ways on
    the same day: keys typed in another window still reaching the game, and a
    sentence typed into a document turning up in the game's chat box.

    That is this, and it is mine. The device is asked for as BACKGROUND now so
    Windows never revokes it, which also means Windows never stops delivering
    it, so whether the game should be listening is this file's job.

    GetForegroundWindow and GetWindowThreadProcessId are kernel calls that
    cannot block. The process comparison rather than a plain handle match is
    deliberate: the game has more than one top level window and the browser
    overlay has its own, and all of them are still the game.
  */
  HWND fg = GetForegroundWindow();
  if (!fg)
    return false;
  if (fg == g_gameWindow)
    return true;

  DWORD pid = 0;
  GetWindowThreadProcessId(fg, &pid);
  return pid == GetCurrentProcessId();
}

/*
  THORNSWOOD PATCH, fourth pass. Ask Windows whether the buffer is telling the
  truth, and put the keyboard back when it is not.

  Every explanation so far has been an attempt to name what takes the keyboard,
  and each one has been shot down by the next log. What the meter did establish
  is much narrower and is not in doubt: the hook is called at full rate, every
  read returns DI_OK, nothing is ever reported lost, and the buffer says no key
  is down while keys are being pressed. Nothing else creates a DirectInput
  device in this process, so it is not another client holding it exclusive.

  DInputHook::Update registers raw input with hwndTarget NULL and no flags,
  which means WM_INPUT is delivered to whichever window holds KEYBOARD FOCUS in
  this process, and DirectInput is built on that delivery. Focus inside the
  process is not the foreground window: the watcher shows the game holding the
  foreground across the whole lock. A foreground change resets focus, which is
  exactly why alt tabbing out and back in has cured it every single time.

  Rather than keep guessing which window takes that focus, this checks the
  device against something that cannot be affected by it. GetAsyncKeyState
  reads the kernel's own key table and answers the same whoever has focus. If
  Windows says a key is held and our buffer says the keyboard is empty, the
  buffer is wrong, and after a second of that the registration is put back,
  which is the alt tab cure performed in place. It logs every time it fires, so
  the cause is still being narrowed even while it stops hurting.

  GetAsyncKeyState is a lookup in a shared table. It sends no message and
  cannot block, which is the rule this file has had to learn twice.
*/
static void CheckAgainstWindows(const uint8_t* apKeyboard)
{
  static unsigned long long disagreeingSince = 0;
  static unsigned long long lastRepair = 0;
  static unsigned long long lastCheck = 0;

  unsigned long long now = GetTickCount64();
  if (now - lastCheck < 250)
    return;
  lastCheck = now;

  bool anyDirectInput = false;
  for (int i = 0; i < 256; ++i) {
    if (apKeyboard[i] & 0x80) {
      anyDirectInput = true;
      break;
    }
  }

  bool anyWindows = false;
  // Mouse buttons live at 0x01 to 0x06 and are somebody else's business.
  for (int vk = 0x08; vk <= 0xFE; ++vk) {
    if (GetAsyncKeyState(vk) & 0x8000) {
      anyWindows = true;
      break;
    }
  }

  if (!anyWindows || anyDirectInput) {
    disagreeingSince = 0;
    return;
  }

  if (!disagreeingSince) {
    disagreeingSince = now;
    return;
  }
  if (now - disagreeingSince < 1000)
    return;
  // Never more than once every three seconds, so a genuine disagreement does
  // not turn into a re-registration loop.
  if (lastRepair && now - lastRepair < 3000)
    return;

  disagreeingSince = 0;
  lastRepair = now;

  HWND had = GetFocus();

  /*
    Two repairs, cheapest first.

    Keyboard focus inside the process is what the raw input registration
    follows, so putting it back on the game's own window is the closest thing
    to what alt tabbing does. SetFocus only works on a window this thread owns
    and fails harmlessly otherwise, which is why it is tried rather than
    depended on. Then the registration goes back regardless, because the two
    failure modes look identical from here and doing both costs nothing.
  */
  if (g_gameWindow)
    SetFocus(g_gameWindow);

  DInputHook::Get().SetEnabled(true);

  FILE* f = InputLog();
  if (f) {
    fprintf(f,
            "[repair] Windows says a key is held and the device says nothing "
            "is. Focus was %p, game window is %p, focus now %p. Raw input "
            "registration put back.\n",
            (void*)had, (void*)g_gameWindow, (void*)GetFocus());
    fflush(f);
  }
}

static bool IsKeyboard(IDirectInputDevice8A* apDevice)
{
  DIDEVICEINSTANCEA info;
  info.dwSize = sizeof(info);
  if (IDirectInputDevice8_GetDeviceInfo(apDevice, &info) != DI_OK)
    return false;
  return info.guidInstance == GUID_SysKeyboard;
}

struct FakeIDirectInputDevice8A
{
  FakeIDirectInputDevice8A(IDirectInputDevice8A* apDevice)
    : m_pDevice(apDevice)
  {
  }

  virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                                   LPVOID* ppvObj) PURE
  {
    return IDirectInputDevice8_QueryInterface(m_pDevice, riid, ppvObj);
  }
  virtual ULONG STDMETHODCALLTYPE AddRef() PURE
  {
    return IDirectInputDevice8_AddRef(m_pDevice);
  }
  virtual ULONG STDMETHODCALLTYPE Release() PURE;

  /*** IDirectInputDevice8A methods ***/
  virtual HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS a) PURE
  {
    return IDirectInputDevice8_GetCapabilities(m_pDevice, a);
  }
  virtual HRESULT STDMETHODCALLTYPE
  EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA a, LPVOID b, DWORD c) PURE
  {
    return IDirectInputDevice8_EnumObjects(m_pDevice, a, b, c);
  }
  virtual HRESULT STDMETHODCALLTYPE GetProperty(REFGUID a,
                                                LPDIPROPHEADER b) PURE
  {
    return IDirectInputDevice8_GetProperty(m_pDevice, a, b);
  }
  virtual HRESULT STDMETHODCALLTYPE SetProperty(REFGUID a,
                                                LPCDIPROPHEADER b) PURE
  {
    return IDirectInputDevice8_SetProperty(m_pDevice, a, b);
  }
  virtual HRESULT STDMETHODCALLTYPE Acquire() PURE
  {
    return IDirectInputDevice8_Acquire(m_pDevice);
  }
  virtual HRESULT STDMETHODCALLTYPE Unacquire() PURE
  {
    return IDirectInputDevice8_Unacquire(m_pDevice);
  }
  virtual HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD a, LPVOID b) PURE;
  virtual HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD a,
                                                  LPDIDEVICEOBJECTDATA b,
                                                  LPDWORD c, DWORD d) PURE;
  virtual HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT a) PURE
  {
    return IDirectInputDevice8_SetDataFormat(m_pDevice, a);
  }
  virtual HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE a) PURE
  {
    return IDirectInputDevice8_SetEventNotification(m_pDevice, a);
  }
  virtual HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND a, DWORD b) PURE;
  virtual HRESULT STDMETHODCALLTYPE GetObjectInfo(LPDIDEVICEOBJECTINSTANCEA a,
                                                  DWORD b, DWORD c) PURE
  {
    return IDirectInputDevice8_GetObjectInfo(m_pDevice, a, b, c);
  }
  virtual HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEA a) PURE
  {
    return IDirectInputDevice8_GetDeviceInfo(m_pDevice, a);
  }
  virtual HRESULT STDMETHODCALLTYPE RunControlPanel(HWND a, DWORD b) PURE
  {
    return IDirectInputDevice8_RunControlPanel(m_pDevice, a, b);
  }
  virtual HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE a, DWORD b,
                                               REFGUID c) PURE
  {
    return IDirectInputDevice8_Initialize(m_pDevice, a, b, c);
  }
  virtual HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID a, LPCDIEFFECT b,
                                                 LPDIRECTINPUTEFFECT* c,
                                                 LPUNKNOWN d) PURE
  {
    return IDirectInputDevice8_CreateEffect(m_pDevice, a, b, c, d);
  }
  virtual HRESULT STDMETHODCALLTYPE EnumEffects(LPDIENUMEFFECTSCALLBACKA a,
                                                LPVOID b, DWORD c) PURE
  {
    return IDirectInputDevice8_EnumEffects(m_pDevice, a, b, c);
  }
  virtual HRESULT STDMETHODCALLTYPE GetEffectInfo(LPDIEFFECTINFOA a,
                                                  REFGUID b) PURE
  {
    return IDirectInputDevice8_GetEffectInfo(m_pDevice, a, b);
  }
  virtual HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD a) PURE
  {
    return IDirectInputDevice8_GetForceFeedbackState(m_pDevice, a);
  }
  virtual HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD a) PURE
  {
    return IDirectInputDevice8_SendForceFeedbackCommand(m_pDevice, a);
  }
  virtual HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(
    LPDIENUMCREATEDEFFECTOBJECTSCALLBACK a, LPVOID b, DWORD c) PURE
  {
    return IDirectInputDevice8_EnumCreatedEffectObjects(m_pDevice, a, b, c);
  }
  virtual HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE a) PURE
  {
    return IDirectInputDevice8_Escape(m_pDevice, a);
  }
  virtual HRESULT STDMETHODCALLTYPE Poll() PURE
  {
    return IDirectInputDevice8_Poll(m_pDevice);
  }
  virtual HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD a,
                                                   LPCDIDEVICEOBJECTDATA b,
                                                   LPDWORD c, DWORD d) PURE
  {
    return IDirectInputDevice8_SendDeviceData(m_pDevice, a, b, c, d);
  }
  virtual HRESULT STDMETHODCALLTYPE EnumEffectsInFile(
    LPCSTR a, LPDIENUMEFFECTSINFILECALLBACK b, LPVOID c, DWORD d) PURE
  {
    return IDirectInputDevice8_EnumEffectsInFile(m_pDevice, a, b, c, d);
  }
  virtual HRESULT STDMETHODCALLTYPE WriteEffectToFile(LPCSTR a, DWORD b,
                                                      LPDIFILEEFFECT c,
                                                      DWORD d) PURE
  {
    return IDirectInputDevice8_WriteEffectToFile(m_pDevice, a, b, c, d);
  }
  virtual HRESULT STDMETHODCALLTYPE BuildActionMap(LPDIACTIONFORMATA a,
                                                   LPCSTR b, DWORD c) PURE
  {
    return IDirectInputDevice8_BuildActionMap(m_pDevice, a, b, c);
  }
  virtual HRESULT STDMETHODCALLTYPE SetActionMap(LPDIACTIONFORMATA a, LPCSTR b,
                                                 DWORD c) PURE
  {
    return IDirectInputDevice8_SetActionMap(m_pDevice, a, b, c);
  }
  virtual HRESULT STDMETHODCALLTYPE
  GetImageInfo(LPDIDEVICEIMAGEINFOHEADERA a) PURE
  {
    return IDirectInputDevice8_GetImageInfo(m_pDevice, a);
  }

private:
  IDirectInputDevice8A* m_pDevice;
};

using TIDirectInputA_CreateDevice =
  HRESULT(_stdcall*)(IDirectInput8A* pDirectInput, REFGUID typeGuid,
                     LPDIRECTINPUTDEVICE8A* apDevice, LPUNKNOWN unused);
using TDirectInput8Create = HRESULT(_stdcall*)(HINSTANCE, DWORD, REFIID,
                                               LPVOID*, LPUNKNOWN);

static TIDirectInputA_CreateDevice RealIDirectInputA_CreateDevice = nullptr;
static TDirectInput8Create RealDirectInput8Create = nullptr;

static Set<FakeIDirectInputDevice8A*> s_devices;

HRESULT _stdcall FakeIDirectInputDevice8A::SetCooperativeLevel(HWND hwnd,
                                                               DWORD flags)
{
  if (hwnd)
    g_gameWindow = hwnd;

  DIDEVICEINSTANCEA info;
  info.dwSize = sizeof(info);
  bool named = IDirectInputDevice8_GetDeviceInfo(m_pDevice, &info) == DI_OK;
  bool keyboard = named && info.guidInstance == GUID_SysKeyboard;

  DWORD asked = flags;

  if (keyboard) {
    /*
      Nobody gets the keyboard to themselves.

      Exclusive and background together is not a legal pair, so exclusive goes
      as well, and that is the point rather than a side effect: while any one
      client holds this device exclusive every other client reads DI_OK and an
      empty buffer, which is the lock exactly. The game never wanted exclusive
      and nothing on this machine has a good reason to take it.
    */
    flags &= ~(DISCL_FOREGROUND | DISCL_EXCLUSIVE);
    flags |= DISCL_BACKGROUND | DISCL_NONEXCLUSIVE;
  }

  HRESULT hr = IDirectInputDevice8_SetCooperativeLevel(m_pDevice, hwnd, flags);
  LogCoop(named ? info.tszProductName : "(unnamed device)", asked, flags);
  return hr;
}

HRESULT _stdcall FakeIDirectInputDevice8A::GetDeviceState(DWORD outDataLen,
                                                          LPVOID outData)
{
  if (!g_listener)
    return DI_OK;
  g_listener->OnUpdate();

  // return IDirectInputDevice8_GetDeviceState(m_pDevice, outDataLen, outData);

  DIDEVICEINSTANCEA instanceInfo;
  instanceInfo.dwSize = sizeof(instanceInfo);
  if (IDirectInputDevice8_GetDeviceInfo(m_pDevice, &instanceInfo) != DI_OK) {
    // TODO: destroy everything
    return DI_OK;
  }

  HRESULT ret =
    IDirectInputDevice8_GetDeviceState(m_pDevice, outDataLen, outData);

  g_meter.stateCalls++;
  if (Lost(ret)) {
    g_meter.lostState++;
    IDirectInputDevice8_Acquire(m_pDevice);
    ret = IDirectInputDevice8_GetDeviceState(m_pDevice, outDataLen, outData);
  }
  g_meter.lastState = static_cast<long>(ret);

  // Zero rather than return early, so the listener below still sees the
  // buttons come up and nothing is left stuck down while minimised.
  if (ret == DI_OK && outData && outDataLen && !ShouldReadInput()) {
    g_meter.minimisedAte++;
    memset(outData, 0, outDataLen);
  }

  bool isMouseButtonsEnabled = true;
  if (isMouseButtonsEnabled == false) {
    DIMOUSESTATE2 fakeMouseState;
    memcpy(&fakeMouseState, outData, outDataLen);
    for (int i = 0; i < std::size(fakeMouseState.rgbButtons); ++i) {
      fakeMouseState.rgbButtons[i] = 0;
    }
    memcpy(outData, &fakeMouseState, outDataLen);
  }

  if (ret != DI_OK)
    return ret;

  DIMOUSESTATE2* mouseState = (DIMOUSESTATE2*)outData;

  ProcessMouseData(mouseState);

  if (DInputHook::ChromeFocus()) {
    // memset(outData, 0, outDataLen);
    DIMOUSESTATE2* mouseState = (DIMOUSESTATE2*)outData;
    for (int i = 0; i < 8; ++i) {
      uint8_t& state = mouseState->rgbButtons[i];
      constexpr int pressed = 0x80;
      state &= ~pressed;
    }
    return 0;
  }
  return DI_OK;
}

HRESULT _stdcall FakeIDirectInputDevice8A::GetDeviceData(
  DWORD dataSize, LPDIDEVICEOBJECTDATA outData, LPDWORD outDataLen,
  DWORD flags)
{
  DInputHook::Get().RunTasks();

  auto& input = DInputHook::Get();

  auto result = IDirectInputDevice8_GetDeviceData(
    m_pDevice, dataSize, outData, outDataLen, flags);

  g_meter.dataCalls++;
  if (Lost(result)) {
    g_meter.lostData++;
    IDirectInputDevice8_Acquire(m_pDevice);
    result = IDirectInputDevice8_GetDeviceData(m_pDevice, dataSize, outData,
                                               outDataLen, flags);
  }
  g_meter.lastData = static_cast<long>(result);
  ReportInput(DInputHook::ChromeFocus(), ShouldReadInput(), g_gameWindow);

  DIDEVICEINSTANCEA instanceInfo;
  instanceInfo.dwSize = sizeof(instanceInfo);
  if (IDirectInputDevice8_GetDeviceInfo(m_pDevice, &instanceInfo) != DI_OK) {
    return result;
  }

  if (instanceInfo.guidInstance == GUID_SysKeyboard) {
    uint8_t rawData[256];
    HRESULT hr = IDirectInputDevice8_GetDeviceState(m_pDevice, 256, rawData);
    g_meter.kbdReads++;
    if (Lost(hr)) {
      g_meter.lostKbd++;
      IDirectInputDevice8_Acquire(m_pDevice);
      hr = IDirectInputDevice8_GetDeviceState(m_pDevice, 256, rawData);
    }
    g_meter.lastKbd = static_cast<long>(hr);
    if (hr == DI_OK) {
      CheckAgainstWindows(rawData);

      // Same reasoning as the mouse: hand the listener an all clear rather
      // than nothing, so a key let go while the window was minimised is not
      // still held when it comes back.
      if (!ShouldReadInput()) {
        memset(rawData, 0, 256);
      }
      ProcessKeyboardData(rawData);
      memset(rawData, 0, 256);
    }
    if (DInputHook::ChromeFocus() || !ShouldReadInput()) {
      if (DInputHook::ChromeFocus()) {
        g_meter.chromeAte++;
      } else {
        g_meter.minimisedAte++;
      }
      *outDataLen = 0;

      return result;
    }
  }

  return result;
}

ULONG _stdcall FakeIDirectInputDevice8A::Release()
{
  const auto result = IDirectInputDevice8_Release(m_pDevice);
  if (result == 0) {
    s_devices.erase(this);

    delete this;
  }

  return result;
}

HRESULT _stdcall HookIDirectInputA_CreateDevice(
  IDirectInput8A* pDirectInput, REFGUID typeGuid,
  LPDIRECTINPUTDEVICE8A* apDevice, LPUNKNOWN unused)
{
  const auto result =
    RealIDirectInputA_CreateDevice(pDirectInput, typeGuid, apDevice, unused);

  if (result == DI_OK) {
    auto pStub = new FakeIDirectInputDevice8A(*apDevice);

    s_devices.insert(pStub);

    *apDevice = reinterpret_cast<LPDIRECTINPUTDEVICE8A>(pStub);
  }

  return result;
}

static HRESULT _stdcall HookDirectInput8Create(HINSTANCE instance,
                                               DWORD version, REFIID iid,
                                               LPVOID* out, LPUNKNOWN outer)
{
  IDirectInput8A* pDirectInput = nullptr;

  const auto result = RealDirectInput8Create(
    instance, version, iid, reinterpret_cast<LPVOID*>(&pDirectInput), outer);

  *out = static_cast<LPVOID>(pDirectInput);

  if (result == DI_OK && RealIDirectInputA_CreateDevice == nullptr) {
    RealIDirectInputA_CreateDevice = pDirectInput->lpVtbl->CreateDevice;
    TP_HOOK_IMMEDIATE(&RealIDirectInputA_CreateDevice,
                      HookIDirectInputA_CreateDevice);
  }

  return result;
}

void DInputHook::Install(std::shared_ptr<IInputListener> listener) noexcept
{
  g_listener = listener;
  TP_HOOK_IAT(DirectInput8Create, "dinput8.dll");
}

DInputHook::DInputHook() noexcept
{
  SetToggleKeys({ DIK_RCONTROL });
}

void DInputHook::SetToggleKeys(
  std::initializer_list<unsigned long> aKeys) noexcept
{
  m_toggleKeys.clear();

  for (auto key : aKeys) {
    m_toggleKeys.insert(key);
  }
}

bool DInputHook::IsToggleKey(unsigned int aKey) const noexcept
{
  return m_toggleKeys.count(aKey) > 0;
}

void DInputHook::Acquire() const noexcept
{
  for (auto& device : s_devices) {
    device->Acquire();
  }
}

void DInputHook::Unacquire() const noexcept
{
  for (auto& device : s_devices) {
    device->Unacquire();
  }
}

DInputHook& DInputHook::Get() noexcept
{
  static DInputHook s_instance;
  return s_instance;
}

void DInputHook::Update() const noexcept
{
  /*
    THORNSWOOD PATCH. The raw input registration is gone, as a bisect.

    Six explanations for the keyboard lock have now been built and shot down by
    the next log. What the meter did establish is not in dispute: DirectInput is
    called at full rate, every read returns DI_OK, nothing is ever reported
    lost, no other client holds the device, nothing here discards a key, and the
    game receives input and does nothing with it. Alt tabbing out and back fixes
    it every single time.

    This function is the only place in the whole hook that reaches outside
    DirectInput and changes state for the entire process. It calls
    RegisterRawInputDevices with hwndTarget NULL and no flags, which replaces
    whatever registration the process already had for the keyboard and the
    mouse, and makes WM_INPUT follow keyboard focus inside the process. Skyrim
    reads raw input too. A foreground change is exactly what re-establishes all
    of it, which is the one behaviour of this bug I have never been able to
    explain any other way.

    So rather than guess again: take it out and see. Nothing in SkyrimPlatform
    needs it. The hook feeds on DirectInput, which is untouched, and Acquire and
    Unacquire still do their jobs below.

    If the lock survives this, that is worth more than a fix would be. It
    exonerates this file completely, and the next place to look is Skyrim's own
    input manager or another SKSE plugin, not here.
  */
  if (m_enabled) {
    Acquire();
  } else {
    Unacquire();
  }
  return;

  RAWINPUTDEVICE device[2];

  device[0].usUsagePage = 0x01;
  device[0].usUsage = 0x06;
  device[0].dwFlags = RIDEV_REMOVE;
  device[0].hwndTarget = nullptr;

  device[1].usUsagePage = 0x01;
  device[1].usUsage = 0x02;
  device[1].dwFlags = RIDEV_REMOVE;
  device[1].hwndTarget = nullptr;

  RegisterRawInputDevices(device, sizeof(device) / sizeof(RAWINPUTDEVICE),
                          sizeof(RAWINPUTDEVICE));

  if (m_enabled) {
    Acquire();

    device[0].dwFlags = 0;
    device[1].dwFlags = 0;

    RegisterRawInputDevices(device, sizeof(device) / sizeof(RAWINPUTDEVICE),
                            sizeof(RAWINPUTDEVICE));
  } else {
    Unacquire();
  }
}
}
