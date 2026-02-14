#pragma once
#include "Module.h"
#include "ThemeTypes.h"
#include "ModulesConcrete.h"

class ModuleFactory
{
public:
	static Module *Create(const std::string &name, const ThemeConfig &theme)
	{
		if (theme.modules.find(name) == theme.modules.end()) {
			OutputDebugStringA(("Warning: Module ID '" + name + "' not found in config.json\n").c_str());
			return nullptr;
		}
		ModuleConfig cfg = theme.modules.at(name);

		if (cfg.type == "custom") return new CustomModule(cfg);
		else if (cfg.type == "clock") return new ClockModule(cfg);
		else if (cfg.type == "workspaces") return new WorkspacesModule(cfg);
		else if (cfg.type == "cpu") return new CpuModule(cfg);
		else if (cfg.type == "gpu") return new GpuModule(cfg);
		else if (cfg.type == "ram") return new RamModule(cfg);
		else if (cfg.type == "ping") return new PingModule(cfg);
		else if (cfg.type == "weather") { return new WeatherModule(cfg); }
		else if (cfg.type == "app_icon") return new AppIconModule(cfg);
		else if (cfg.type == "dock") return new DockModule(cfg);
		else if (cfg.type == "visualizer") return new VisualizerModule(cfg, Railing::instance->visualizerBackend);

		else if (cfg.type == "group") {
			GroupModule *group = new GroupModule(cfg);
			for (const auto &childName : cfg.groupModules) {
				Module *child = Create(childName, theme);
				if (child) group->AddChild(child);
			}
			return group;
		}
		else if (cfg.type == "network" || cfg.type == "audio" || cfg.type == "battery"
			|| cfg.type == "tray" || cfg.type == "notification") return new IconModule(cfg);
		return new IconModule(cfg);
		return nullptr; // UNK
	}
};