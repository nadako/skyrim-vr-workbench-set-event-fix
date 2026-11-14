namespace {
	void SendFurnitureEvent(RE::Actor *actor, RE::TESObjectREFR *furniture, RE::TESFurnitureEvent::FurnitureEventType eventType) {
		RE::TESFurnitureEvent event{};
		event.actor = RE::NiPointer<RE::TESObjectREFR>(actor);
		event.targetFurniture = RE::NiPointer<RE::TESObjectREFR>(furniture);
		event.type = eventType;
		RE::ScriptEventSourceHolder::GetSingleton()->SendEvent(&event);
	}

	RE::ObjectRefHandle lastOccupiedFurniture;

	void EnterLastOccupiedFurniture(RE::Actor *actor, RE::ObjectRefHandle &furniture) {
		auto furniturePtr = furniture.get();
		lastOccupiedFurniture = furniture;
		if (furniturePtr) {
			SendFurnitureEvent(actor, furniturePtr.get(), RE::TESFurnitureEvent::FurnitureEventType::kEnter);
		}
	}

	void ExitLastOccupiedFurniture(RE::Actor *actor) {
		auto furniturePtr = lastOccupiedFurniture.get();
		lastOccupiedFurniture.reset();
		if (furniturePtr) {
			SendFurnitureEvent(actor, furniturePtr.get(), RE::TESFurnitureEvent::FurnitureEventType::kExit);
		}
	}

	struct FurnitureActivateSetOccupiedFurniture {
		static void thunk(RE::AIProcess *aiProcess, RE::Actor *actor, RE::ObjectRefHandle *furnitureHandle) {
			original(aiProcess, actor, furnitureHandle);

			if (!actor || !actor->IsPlayerRef() || !furnitureHandle) return;

			auto occupiedFurnitureHandle = actor->GetOccupiedFurniture();
			if (!occupiedFurnitureHandle || occupiedFurnitureHandle != *furnitureHandle) return;

			// somehow Activate was called again before we cleared the occupied furniture, let's send the exit event I guess...
			if (lastOccupiedFurniture) ExitLastOccupiedFurniture(actor);

			EnterLastOccupiedFurniture(actor, occupiedFurnitureHandle);
		}

		static inline REL::Relocation<decltype(thunk)> original;
	};

	struct QuitMenuClearFurniture {
		static void thunk(RE::AIProcess *aiProcess, RE::Actor *actor) {
			if (!actor || !actor->IsPlayerRef() || !lastOccupiedFurniture) {
				original(aiProcess, actor);
				return;
			}

			auto prevOccupiedFurniture = actor->GetOccupiedFurniture();
			original(aiProcess, actor);

			if (prevOccupiedFurniture != lastOccupiedFurniture) return;

			// just in case some other hook would prevent clearing the furniture
			auto newOccupiedFurniture = actor->GetOccupiedFurniture();
			if (newOccupiedFurniture == prevOccupiedFurniture) return;

			ExitLastOccupiedFurniture(actor);
		}

		static inline REL::Relocation<decltype(thunk)> original;
	};

	struct PlayerUpdate {
		static void thunk(RE::PlayerCharacter *player, float delta) {
			original(player, delta);
			if (lastOccupiedFurniture) {
				auto currentOccupiedFurniture = player->GetOccupiedFurniture();
				if (currentOccupiedFurniture != lastOccupiedFurniture) {
					// occupation somehow ended outside QuitMenu, let's also send the exit event, I guess
					ExitLastOccupiedFurniture(player);
				}
			}
		}

		static inline REL::Relocation<decltype(thunk)> original;
	};

	bool IsNearRelativeCallTo(uintptr_t callAddress, uintptr_t targetAddress) {
		auto bytes = (uint8_t *) callAddress;
		if (bytes[0] != 0xE8) return false; // not a near relative call, someone hooked it already?
		int32_t rel32;
		memcpy(&rel32, bytes + 1, 4);
		uintptr_t calledAddress = callAddress + 5 + rel32;
		return calledAddress == targetAddress;
	}

	bool InstallHooks() {
		constexpr uintptr_t furnitureActivateSetOccupiedCallOffset = 0x22BE5A;
		constexpr uintptr_t quitMenuClearFurnitureCallOffset = 0x896E27;
		constexpr uintptr_t setOccupiedFuncOffset = 0x68A100;
		constexpr uintptr_t clearFurnitureFuncOffset = 0x683090;
		constexpr size_t playerUpdateVTableSlotIndex = 175;

		uintptr_t furnitureActivateSetOccupiedCall = REL::Offset(furnitureActivateSetOccupiedCallOffset).address();
		uintptr_t quitMenuClearFurnitureCall = REL::Offset(quitMenuClearFurnitureCallOffset).address();
		bool callsAreVanilla =
				IsNearRelativeCallTo(furnitureActivateSetOccupiedCall, REL::Offset(setOccupiedFuncOffset).address()) &&
				IsNearRelativeCallTo(quitMenuClearFurnitureCall, REL::Offset(clearFurnitureFuncOffset).address());
		if (!callsAreVanilla) {
			logger::error("Unexpected code at hook sites, exiting!");
			return false;
		}

		auto &trampoline = SKSE::GetTrampoline();
		FurnitureActivateSetOccupiedFurniture::original = trampoline.write_call<5>(furnitureActivateSetOccupiedCall, FurnitureActivateSetOccupiedFurniture::thunk);
		QuitMenuClearFurniture::original = trampoline.write_call<5>(quitMenuClearFurnitureCall, QuitMenuClearFurniture::thunk);

		REL::Relocation<uintptr_t> vtable_PlayerCharacter{RE::PlayerCharacter::VTABLE[0]};
		PlayerUpdate::original = vtable_PlayerCharacter.write_vfunc(playerUpdateVTableSlotIndex, PlayerUpdate::thunk);

		logger::info("Installed workbench event hooks");
		return true;
	}

	void OnSKSEMessage(SKSE::MessagingInterface::Message *message) {
		switch (message->type) {
			case SKSE::MessagingInterface::kPreLoadGame:
			case SKSE::MessagingInterface::kNewGame: {
				lastOccupiedFurniture.reset();
				break;
			}
			default:
				break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {
	SKSE::Init(skse, {.trampoline = true, .trampolineSize = 28}); // two hooks, 14 (6+8) bytes each

	auto plugin = SKSE::PluginDeclaration::GetSingleton();
	logger::info("Initializing version: {}", plugin->GetVersion());

	bool success = InstallHooks();
	if (success) {
		SKSE::GetMessagingInterface()->RegisterListener(OnSKSEMessage);
		logger::info("Initialization complete.");
	} else {
		logger::error("Initialization aborted.");
	}
	return true;
}
