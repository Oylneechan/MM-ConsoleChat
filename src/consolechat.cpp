#include <stdio.h>
#include "consolechat.h"
#include "module.h"
#include "utils/plat.h"
#include "funchook.h"
#include <string>

#include <ccsplayercontroller.h>
#include <tier1/strtools.h>
#include <cgamerules.h>

CCSGameRules* g_pGameRules = nullptr;
CGlobalVars* gpGlobals = nullptr;

typedef void (FASTCALL* UTIL_SayTextFilter_t)(IRecipientFilter&, const char*, CCSPlayerController*, uint64);

void FASTCALL UTIL_SayTextFilter(IRecipientFilter&, const char*, CCSPlayerController*, uint64);

UTIL_SayTextFilter_t g_SayTextFilter = nullptr;

funchook_t* g_pSayTextFilter = nullptr;

ConsoleChat g_ConsoleChat;

PLUGIN_EXPOSE(ConsoleChat, g_ConsoleChat);
bool ConsoleChat::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
	g_SMAPI->AddListener(this, this);

	CModule* pServer = new CModule(ROOTBIN, "server");

#ifdef PLATFORM_WINDOWS
	const byte Signature[] = "\x48\x89\x5C\x24\x2A\x55\x56\x57\x48\x8D\x6C\x24\x2A\x48\x81\xEC\x2A\x2A\x2A\x2A\x49\x8B\xD8";
#else
	const byte Signature[] = "\x55\x48\x89\xE5\x41\x57\x49\x89\xD7\x31\xD2\x41\x56\x4C\x8D\x75\x98";
#endif

	int sig_error;

	g_SayTextFilter = (UTIL_SayTextFilter_t)pServer->FindSignature(Signature, sizeof(Signature) - 1, sig_error);

	if (!g_SayTextFilter)
	{
		return false;
	}

	delete pServer;

	g_pSayTextFilter = funchook_create();
	funchook_prepare(g_pSayTextFilter, (void**)&g_SayTextFilter, (void*)UTIL_SayTextFilter);
	funchook_install(g_pSayTextFilter, 0);

	return true;
}

bool ConsoleChat::Unload(char* error, size_t maxlen)
{
	if (g_pSayTextFilter)
	{
		funchook_uninstall(g_pSayTextFilter, 0);
		funchook_destroy(g_pSayTextFilter);
	}
	return true;
}

void FASTCALL UTIL_SayTextFilter(IRecipientFilter& filter, const char* pText, CCSPlayerController* pPlayer, uint64 eMessageType)
{
	if (pPlayer)
		return UTIL_SayTextFilter(filter, pText, pPlayer, eMessageType);

	SayChatMessageWithTimer(filter, pText, pPlayer, eMessageType);
}

void SayChatMessageWithTimer(IRecipientFilter& filter, const char* pText, CCSPlayerController* pPlayer, uint64 eMessageType)
{
	char buf[256];

	// Filter console message - remove non-alphanumeric chars and convert to lowercase
	uint32 uiTextLength = strlen(pText);
	uint32 uiFilteredTextLength = 0;
	char filteredText[256];

	for (uint32 i = 0; i < uiTextLength; i++)
	{
		if (pText[i] >= 'A' && pText[i] <= 'Z')
			filteredText[uiFilteredTextLength++] = pText[i] + 32;
		if (pText[i] == ' ' || (pText[i] >= '0' && pText[i] <= '9') || (pText[i] >= 'a' && pText[i] <= 'z'))
			filteredText[uiFilteredTextLength++] = pText[i];
	}
	filteredText[uiFilteredTextLength] = '\0';

	// Split console message into words seperated by the space character
	CUtlVector<char*, CUtlMemory<char*, int>> words;
	V_SplitString(filteredText, " ", words);

	//Word count includes the first word "Console:" at index 0, first relevant word is at index 1
	int iWordCount = words.Count();
	uint32 uiTriggerTimerLength = 0;

	if (iWordCount == 2)
		uiTriggerTimerLength = V_StringToUint32(words.Element(1), 0, NULL, NULL, PARSING_FLAG_SKIP_WARNING);

	for (int i = 1; i < iWordCount && uiTriggerTimerLength == 0; i++)
	{
		uint32 uiCurrentValue = V_StringToUint32(words.Element(i), 0, NULL, NULL, PARSING_FLAG_SKIP_WARNING);
		uint32 uiNextWordLength = 0;
		char* pNextWord = NULL;

		if (i + 1 < iWordCount)
		{
			pNextWord = words.Element(i + 1);
			uiNextWordLength = strlen(pNextWord);
		}

		// Case: ... X sec(onds) ... or ... X min(utes) ...
		if (pNextWord != NULL && uiNextWordLength > 2 && uiCurrentValue > 0)
		{
			if (pNextWord[0] == 's' && pNextWord[1] == 'e' && pNextWord[2] == 'c')
				uiTriggerTimerLength = uiCurrentValue;
			if (pNextWord[0] == 'm' && pNextWord[1] == 'i' && pNextWord[2] == 'n')
				uiTriggerTimerLength = uiCurrentValue * 60;
		}

		// Case: ... Xs - only support up to 3 digit numbers (in seconds) for this timer parse method
		if (uiCurrentValue == 0)
		{
			char* pCurrentWord = words.Element(i);
			uint32 uiCurrentScanLength = MIN(strlen(pCurrentWord), 4);

			for (uint32 j = 0; j < uiCurrentScanLength; j++)
			{
				if (pCurrentWord[j] >= '0' && pCurrentWord[j] <= '9')
					continue;

				if (pCurrentWord[j] == 's')
				{
					pCurrentWord[j] = '\0';
					uiTriggerTimerLength = V_StringToUint32(pCurrentWord, 0, NULL, NULL, PARSING_FLAG_SKIP_WARNING);
				}
				break;
			}
		}
	}
	words.PurgeAndDeleteElements();

	float fCurrentRoundClock = g_pGameRules->m_iRoundTime - (gpGlobals->curtime - g_pGameRules->m_fRoundStartTime.Get().m_Value);

	// Only display trigger time if the timer is greater than 4 seconds, and time expires within the round
	if ((uiTriggerTimerLength > 4) && (fCurrentRoundClock > uiTriggerTimerLength))
	{
		int iTriggerTime = fCurrentRoundClock - uiTriggerTimerLength;

		// Round timer to nearest whole second
		if ((int)(fCurrentRoundClock - 0.5f) == (int)fCurrentRoundClock)
			iTriggerTime++;

		int mins = iTriggerTime / 60;
		int secs = iTriggerTime % 60;

		V_snprintf(buf, sizeof(buf), "%s %s %s %2d:%02d", " \7CONSOLE:\4", pText + sizeof("Console:"), "\x10- @", mins, secs);
	}
	else
		V_snprintf(buf, sizeof(buf), "%s %s", " \7CONSOLE:\4", pText + sizeof("Console:"));

	UTIL_SayTextFilter(filter, buf, pPlayer, eMessageType);
}

const char* ConsoleChat::GetLicense()
{
	return "GPL v3 License";
}

const char* ConsoleChat::GetVersion()
{
	return "1.0";
}

const char* ConsoleChat::GetDate()
{
	return __DATE__;
}

const char* ConsoleChat::GetLogTag()
{
	return "ConsoleChat";
}

const char* ConsoleChat::GetAuthor()
{
	return "Oylsister Original from Source2ZE";
}

const char* ConsoleChat::GetDescription()
{
	return "ConsoleChat";
}

const char* ConsoleChat::GetName()
{
	return "ConsoleChat";
}

const char* ConsoleChat::GetURL()
{
	return "";
}