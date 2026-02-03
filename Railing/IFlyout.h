#pragma once
#include <vector>
#include <algorithm>

class IFlyout {
public:
	virtual void Hide() = 0;
	virtual bool IsVisible() = 0;
};

/// <summary>
/// Manages the registration and visibility of flyout UI components.
/// </summary>
class FlyoutManager {
private:
	std::vector<IFlyout *> flyouts;
	FlyoutManager() {}
public:
	/// <summary>
	///  Gets the singleton instance of the FlyoutManager.
	/// </summary>
	/// <returns>this</returns>
	static FlyoutManager &Get() {
		static FlyoutManager instance;
		return instance;
	}
	/// <summary>
	/// Registers a flyout by adding it to the internal collection.
	/// </summary>
	/// <param name="flyout">Pointer to the IFlyout to register; added to the internal flyouts list.</param>
	void Register(IFlyout *flyout) {
		flyouts.push_back(flyout);
	}
	/// <summary>
	/// Unregisters a flyout by removing it from the internal collection.
	/// </summary>
	/// <param name="flyout">Pointer to the IFlyout to unregister</param>
	void Unregister(IFlyout *flyout) {
		flyouts.erase(std::remove(flyouts.begin(), flyouts.end(), flyout), flyouts.end());
	}
	/// <summary>
	/// Closes all flyouts except the specified current one.
	/// </summary>
	/// <param name="current">The flyout to leave open</param>
	void CloseOthers(IFlyout *current) {
		for (IFlyout *flyout : flyouts) {
			if (flyout != current && flyout->IsVisible()) {
				flyout->Hide();
			}
		}
	}
};