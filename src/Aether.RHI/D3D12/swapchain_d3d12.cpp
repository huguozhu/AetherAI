module;
#include "d3d12_common.h"
module aether.rhi;

#include "device_d3d12_impl.h"

import aether.core;
import <utility>;
import <memory>;
import <vector>;

namespace aether::rhi {

// === SwapChainD3D12 ===
struct SwapChainD3D12 : public SwapChain {
    ComPtr<IDXGISwapChain4> m_swapChain;
    std::vector<TexturePtr> m_backBuffers;
    ID3D12Device10* m_device;
    ID3D12CommandQueue* m_directQueue;
    uint32_t m_bufferCount = 3;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    uint32_t m_rtvDescriptorSize = 0;

    uint32_t get_current_index() const override {
        return m_swapChain->GetCurrentBackBufferIndex();
    }

    TexturePtr get_back_buffer(uint32_t index) override {
        if (index < m_backBuffers.size()) {
            return m_backBuffers[index];
        }
        return nullptr;
    }

    void present(bool vsync) override {
        m_swapChain->Present(vsync ? 1 : 0, 0);
    }

    void resize(uint32_t width, uint32_t height) override {
        // Release back buffers
        m_backBuffers.clear();

        // Resize swap chain buffers
        DXGI_SWAP_CHAIN_DESC1 scDesc{};
        m_swapChain->GetDesc1(&scDesc);

        m_swapChain->ResizeBuffers(
            m_bufferCount, width, height,
            scDesc.Format,
            DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);

        // Re-create back buffer textures
        create_back_buffers();
    }

    void create_back_buffers() {
        m_backBuffers.resize(m_bufferCount);

        // Create RTV descriptor heap
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.NumDescriptors = m_bufferCount;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap));

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        // Create RTV for each back buffer
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

        for (uint32_t i = 0; i < m_bufferCount; ++i) {
            ComPtr<ID3D12Resource> backBuffer;
            m_swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer));

            m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);

            // Wrap in our Texture abstraction
            auto tex = std::make_shared<TextureD3D12>();
            tex->resource = backBuffer;
            D3D12_RESOURCE_DESC backBufferDesc = backBuffer->GetDesc();
            tex->desc.width = static_cast<uint32_t>(backBufferDesc.Width);
            tex->desc.height = backBufferDesc.Height;
            tex->desc.format = Format::R8G8B8A8_UNORM;
            tex->initialState = D3D12_RESOURCE_STATE_PRESENT;
            tex->rtvHandle = rtvHandle;
            tex->hasRtvHandle = true;
            m_backBuffers[i] = std::move(tex);

            rtvHandle.ptr += m_rtvDescriptorSize;
        }
    }
};

// === Device method ===
SwapChainPtr DeviceD3D12::create_swap_chain(const SwapChainDesc& desc) {
    auto sc = std::make_shared<SwapChainD3D12>();
    sc->m_device = m_device.Get();
    sc->m_directQueue = m_directQueue.Get();
    sc->m_bufferCount = desc.bufferCount;

    DXGI_SWAP_CHAIN_DESC1 scDesc{};
    scDesc.Width = desc.width;
    scDesc.Height = desc.height;
    scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferCount = desc.bufferCount;
    scDesc.SampleDesc.Count = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    ComPtr<IDXGISwapChain1> tempSwapChain;
    HRESULT hr = m_factory->CreateSwapChainForHwnd(
        m_directQueue.Get(),
        static_cast<HWND>(desc.windowHandle),
        &scDesc, nullptr, nullptr, &tempSwapChain);

    if (FAILED(hr)) {
        aether::log::error("Failed to create swap chain");
        return nullptr;
    }

    tempSwapChain.As(&sc->m_swapChain);
    sc->create_back_buffers();

    aether::log::info("Created swap chain: {}x{} with {} buffers",
                       desc.width, desc.height, desc.bufferCount);

    return sc;
}

} // namespace aether::rhi
