// Synthetic, loadable VST3 fixture whose advertised effect cannot be created.
#include "pluginterfaces/base/ipluginbase.h"

#include <cstring>

using namespace Steinberg;

namespace {
class FactoryOnly final : public IPluginFactory {
public:
	tresult PLUGIN_API queryInterface(const TUID iid, void **object) override
	{
		*object = nullptr;
		if (FUnknownPrivate::iidEqual(iid, IPluginFactory::iid) ||
		    FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
			*object = static_cast<IPluginFactory *>(this);
			return kResultOk;
		}
		return kNoInterface;
	}
	uint32 PLUGIN_API addRef() override { return 1000; }
	uint32 PLUGIN_API release() override { return 1000; }
	tresult PLUGIN_API getFactoryInfo(PFactoryInfo *info) override
	{
		*info = PFactoryInfo("OBS regression fixture", "", "", 0);
		return kResultOk;
	}
	int32 PLUGIN_API countClasses() override { return 1; }
	tresult PLUGIN_API getClassInfo(int32 index, PClassInfo *info) override
	{
		if (index != 0) {
			return kInvalidArgument;
		}
		const TUID id = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
		*info = PClassInfo(id, PClassInfo::kManyInstances, "Audio Module Class", "Uninitializable effect");
		return kResultOk;
	}
	tresult PLUGIN_API createInstance(FIDString, FIDString, void **object) override
	{
		*object = nullptr;
		return kNoInterface;
	}
};
FactoryOnly factory;
} // namespace

extern "C" __declspec(dllexport) IPluginFactory *PLUGIN_API GetPluginFactory()
{
	return &factory;
}
extern "C" __declspec(dllexport) bool InitDll()
{
	return true;
}
extern "C" __declspec(dllexport) bool ExitDll()
{
	return true;
}
