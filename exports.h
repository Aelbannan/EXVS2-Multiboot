//  exports.h
//
//	Simple header to instruct the linker to forward function exports to another library.
//

#pragma comment(linker,"/export:ApplyCompatResolutionQuirking=dxgi_orig.ApplyCompatResolutionQuirking,@1")
#pragma comment(linker,"/export:CompatString=dxgi_orig.CompatString,@2")
#pragma comment(linker,"/export:CompatValue=dxgi_orig.CompatValue,@3")
#pragma comment(linker,"/export:CreateDXGIFactory=dxgi_orig.CreateDXGIFactory,@10")
#pragma comment(linker,"/export:CreateDXGIFactory1=dxgi_orig.CreateDXGIFactory1,@11")
#pragma comment(linker,"/export:CreateDXGIFactory2=dxgi_orig.CreateDXGIFactory2,@12")
#pragma comment(linker,"/export:DXGID3D10CreateDevice=dxgi_orig.DXGID3D10CreateDevice,@13")
#pragma comment(linker,"/export:DXGID3D10CreateLayeredDevice=dxgi_orig.DXGID3D10CreateLayeredDevice,@14")
#pragma comment(linker,"/export:DXGID3D10GetLayeredDeviceSize=dxgi_orig.DXGID3D10GetLayeredDeviceSize,@15")
#pragma comment(linker,"/export:DXGID3D10RegisterLayers=dxgi_orig.DXGID3D10RegisterLayers,@16")
#pragma comment(linker,"/export:DXGIDeclareAdapterRemovalSupport=dxgi_orig.DXGIDeclareAdapterRemovalSupport,@17")
#pragma comment(linker,"/export:DXGIDisableVBlankVirtualization=dxgi_orig.DXGIDisableVBlankVirtualization,@18")
#pragma comment(linker,"/export:DXGIDumpJournal=dxgi_orig.DXGIDumpJournal,@4")
#pragma comment(linker,"/export:DXGIGetDebugInterface1=dxgi_orig.DXGIGetDebugInterface1,@19")
#pragma comment(linker,"/export:DXGIReportAdapterConfiguration=dxgi_orig.DXGIReportAdapterConfiguration,@20")
#pragma comment(linker,"/export:PIXBeginCapture=dxgi_orig.PIXBeginCapture,@5")
#pragma comment(linker,"/export:PIXEndCapture=dxgi_orig.PIXEndCapture,@6")
#pragma comment(linker,"/export:PIXGetCaptureState=dxgi_orig.PIXGetCaptureState,@7")
#pragma comment(linker,"/export:SetAppCompatStringPointer=dxgi_orig.SetAppCompatStringPointer,@8")
#pragma comment(linker,"/export:UpdateHMDEmulationStatus=dxgi_orig.UpdateHMDEmulationStatus,@9")
