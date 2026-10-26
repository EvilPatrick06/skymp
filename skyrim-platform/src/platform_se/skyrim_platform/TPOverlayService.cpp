#include "TPOverlayService.h"

#include "BrowserApi.h"
#include "BrowserApiTilted.h"
#include "Settings.h"
#include "TPRenderSystemD3D11.h"

using CEFUtils::DX11RenderHandler;
using CEFUtils::MyRenderHandler;

struct D3D11RenderProvider final
  : MyChromiumApp::RenderProvider
  , DX11RenderHandler::Renderer
{
  explicit D3D11RenderProvider(RenderSystemD3D11* apRenderSystem)
    : m_pRenderSystem(apRenderSystem)
  {
  }

  MyRenderHandler* Create() override { return new DX11RenderHandler(this); }

  [[nodiscard]] HWND GetWindow() override
  {
    return m_pRenderSystem->GetWindow();
  }

  [[nodiscard]] IDXGISwapChain* GetSwapChain() const noexcept override
  {
    return m_pRenderSystem->GetSwapChain();
  }

private:
  RenderSystemD3D11* m_pRenderSystem;
};

static OverlayService* g_overlayService = nullptr;

OverlayService::OverlayService(
  std::shared_ptr<ProcessMessageListener> onProcessMessage_,
  const ObtainTextsToDrawFunction& obtainTextsToDraw_)
  : onProcessMessage(onProcessMessage_)
  , obtainTextsToDraw(obtainTextsToDraw_)
{
  if (g_overlayService != nullptr) {
    throw std::runtime_error("OverlayService must be created only once");
  }
  g_overlayService = this;
}

OverlayService::~OverlayService() noexcept
{
  g_overlayService = nullptr;
}

OverlayService* OverlayService::GetInstance()
{
  return g_overlayService;
}

void OverlayService::Create(RenderSystemD3D11* apRenderSystem)
{
  auto renderProvider = std::make_unique<D3D11RenderProvider>(apRenderSystem);
  overlay = new MyChromiumApp(std::move(renderProvider), onProcessMessage);

  overlay->Initialize(BrowserApi::GetBackend() ==
                      BrowserApi::Backend::kTilted);
  overlay->GetClient()->Create();

  // Thornswood #3129: the page starts loading here, so it is shown from here
  // and not from the first plugin tick after the game's data has loaded.
  if (BrowserApi::GetBackend() == BrowserApi::Backend::kTilted &&
      BrowserApi::ShowAtStart()) {
    BrowserApiTilted::ShowAtStart();
    logger::info("browser (tilted): shown and focused at start");
  }
}

void OverlayService::Render() const
{
  overlay->GetClient()->Render(obtainTextsToDraw);
}

void OverlayService::Reset() const
{
  overlay->GetClient()->Reset();
}
