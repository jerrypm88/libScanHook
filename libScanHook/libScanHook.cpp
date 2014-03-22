#include "libScanHook.h"

namespace libScanHook
{
	ScanHook::ScanHook()
	{
		IsRedirction = 0;
		IsFromRedirction = 0;
		ElevatedPriv();
	}

	bool ScanHook::InitScan(DWORD Pid)
	{
		bool ret = 0;
		if (QuerySystemInfo())
		{
			hProcess = OpenProcess(PROCESS_ALL_ACCESS, 0, Pid);
			if (hProcess)
			{
				if (QueryModuleInfo())
				{
					for (ModuleInfoiter = ModuleInfo.begin(); ModuleInfoiter != ModuleInfo.end(); ++ModuleInfoiter)
					{
						ScanEatHook();
						ScanIatHook();
					}
					ret = 1;
					HookInfoiter = HookInfo.begin();
				}
				CloseHandle(hProcess);
			}
		}
		return ret;
	}

	void ScanHook::CloseScan()
	{
		for (ModuleInfoiter = ModuleInfo.begin(); ModuleInfoiter != ModuleInfo.end(); ++ModuleInfoiter)
		{
			if (ModuleInfoiter->ScanBuffer)
				delete[] ModuleInfoiter->ScanBuffer;
			if (ModuleInfoiter->OrigBuffer)
				delete[] ModuleInfoiter->OrigBuffer;
		}
		ModuleInfo.clear();
		HookInfo.clear();
	}

	bool ScanHook::GetProcessHookInfo(PPROCESS_HOOK_INFO Entry)
	{
		bool ret = 0;
		if (HookInfoiter != HookInfo.end())
		{
			Entry->HookType = HookInfoiter->HookType;
			Entry->OriginalAddress = HookInfoiter->OriginalAddress;
			Entry->HookAddress = HookInfoiter->HookAddress;
			wcscpy_s(Entry->HookedApiName, 128, HookInfoiter->HookedApiName);
			wcscpy_s(Entry->HookedModule, 64, HookInfoiter->HookedModule);
			wcscpy_s(Entry->HookLocation, 260, HookInfoiter->HookLocation);
			++HookInfoiter;
			ret = 1;
		}
		return ret;
	}

	bool ScanHook::ScanInlineHook(char *ApiName, DWORD Address)
	{
		bool ret = 0, IsHook = 0;
		DWORD Dest, Src, Index, InstrLen;
		vector<MODULE_INFO>::iterator iter;
		INSTRUCTION Instr, Instr2;
		PROCESS_HOOK_INFO Info;
		memset(&Info, 0, sizeof(PROCESS_HOOK_INFO));
		if (GetModuleInfomation(Address, iter))
		{
			Dest = Address - iter->DllBase + (DWORD)iter->ScanBuffer;
			Src = Address - iter->DllBase + (DWORD)iter->OrigBuffer;
			for (Index = 0; Index < 10; ++Index)
			{
				if ((*(BYTE *)(Dest + Index)) != (*(BYTE *)(Src + Index)))
				{
					InstrLen = get_instruction(&Instr, ((BYTE *)(Dest + Index)), MODE_32);
					switch (Instr.type)
					{
					case INSTRUCTION_TYPE_JMP:
					{
						if (Instr.length == 7)
							Info.HookAddress = Instr.op1.displacement;
						if (Instr.length == 5)
							Info.HookAddress = Dest + Index + Instr.op1.displacement;
						IsHook = 1;
						break;
					}
					case INSTRUCTION_TYPE_PUSH:
					{
						InstrLen = get_instruction(&Instr2, (BYTE *)(Dest + Index + InstrLen), MODE_32);
						if (Instr2.type == INSTRUCTION_TYPE_RET)
						{
							Info.HookAddress = Instr.op1.displacement;
							IsHook = 1;
						}
						break;
					}
					default:
						break;
					}
					break;
				}
			}
			if (IsHook)
			{
				Info.HookType = InlineHook;
				Info.OriginalAddress = Address;
				MultiByteToWideChar(CP_ACP, 0, ApiName, strlen(ApiName) + 1, Info.HookedApiName, 128);
				wcscpy_s(Info.HookedModule, 64, ModuleInfoiter->BaseName);
				GetModulePathByAddress(Info.HookAddress, Info.HookLocation);
				HookInfo.push_back(Info);
			}
		}
		return ret;
	}


	bool ScanHook::ScanEatHook()
	{
		bool ret = 0, IsDllRedirection = 0;
		char *ApiName;
		WORD *NameOrd;
		DWORD i, ApiAddress, OriApiAddress, Tem;
		DWORD *Ent, *Eat, *OriEat;
		PE_INFO Pe, OrigPe;
		PROCESS_HOOK_INFO Info;
		vector<MODULE_INFO>::iterator iter;
		PIMAGE_EXPORT_DIRECTORY ExporTable, OrigExportTable;
		memset(&Info, 0, sizeof(PROCESS_HOOK_INFO));
		if (ParsePe((DWORD)ModuleInfoiter->ScanBuffer, &Pe) && ParsePe((DWORD)ModuleInfoiter->OrigBuffer, &OrigPe))
		{
			if (Pe.ExportSize)
			{
				ExporTable = (PIMAGE_EXPORT_DIRECTORY)((DWORD)ModuleInfoiter->ScanBuffer + Pe.ExportTableRva);
				OrigExportTable = (PIMAGE_EXPORT_DIRECTORY)((DWORD)ModuleInfoiter->OrigBuffer + Pe.ExportTableRva);
				Eat = (DWORD *)((DWORD)ModuleInfoiter->ScanBuffer + ExporTable->AddressOfFunctions);
				Ent = (DWORD *)((DWORD)ModuleInfoiter->ScanBuffer + ExporTable->AddressOfNames);
				NameOrd = (WORD *)((DWORD)ModuleInfoiter->ScanBuffer + ExporTable->AddressOfNameOrdinals);
				OriEat = (DWORD *)((DWORD)ModuleInfoiter->OrigBuffer + OrigExportTable->AddressOfFunctions);
				for (i = 0; i < ExporTable->NumberOfNames; ++i)
				{
					if (IsGlobalVar(OrigPe.PeHead, OriEat[NameOrd[i]]))
						continue;
					ApiName = (char *)(Ent[i] + ModuleInfoiter->DllBase);
					ApiAddress = Eat[NameOrd[i]] + ModuleInfoiter->DllBase;
					OriApiAddress = OriEat[NameOrd[i]] + ModuleInfoiter->DllBase;
					Tem = OriEat[NameOrd[i]] + (DWORD)ModuleInfoiter->OrigBuffer;
					if (Tem >= (DWORD)OrigExportTable && Tem < ((DWORD)OrigExportTable + Pe.ExportSize))
						OriApiAddress = FileNameRedirection(ModuleInfoiter->DllBase, (char *)OriApiAddress);
					ScanInlineHook(ApiName, OriApiAddress);
					if (Eat[NameOrd[i]] != OriEat[NameOrd[i]])
					{
						Info.HookType = EatHook;
						Info.OriginalAddress = OriApiAddress;
						Info.HookAddress = ApiAddress;
						memset(Info.HookedApiName, 0, 64);
						MultiByteToWideChar(CP_ACP, 0, ApiName, strlen(ApiName) + 1, Info.HookedApiName, 128);
						memset(Info.HookedModule, 0, 64);
						wcscpy_s(Info.HookedModule, 64, ModuleInfoiter->BaseName);
						GetModulePathByAddress(ApiAddress, Info.HookLocation);
						HookInfo.push_back(Info);
					}
				}
				ret = 1;
			}
		}
		return ret;
	}

	bool ScanHook::ScanIatHook()
	{
		bool ret = 0, IsApiSet;
		char *DllName, *ApiName;
		char OrdinalName[13];
		WCHAR RealDllName[64];
		WORD Ordinal;
		DWORD ApiAddress, OriApiAddress;
		PIMAGE_THUNK_DATA FirstThunk, OriThunk;
		PIMAGE_IMPORT_BY_NAME ByName;
		PE_INFO Pe;
		PROCESS_HOOK_INFO Info;
		PIMAGE_IMPORT_DESCRIPTOR ImportTable;
		memset(&Info, 0, sizeof(PROCESS_HOOK_INFO));
		if (ParsePe((DWORD)ModuleInfoiter->ScanBuffer, &Pe))
		{
			if (Pe.ImportSize)
			{
				ImportTable = (PIMAGE_IMPORT_DESCRIPTOR)((DWORD)ModuleInfoiter->ScanBuffer + Pe.ImportTableRva);
				while (ImportTable->FirstThunk)
				{
					DllName = (char *)(ImportTable->Name + (DWORD)ModuleInfoiter->ScanBuffer);
					OriThunk = (PIMAGE_THUNK_DATA)(ImportTable->OriginalFirstThunk + (DWORD)ModuleInfoiter->ScanBuffer);
					FirstThunk = (PIMAGE_THUNK_DATA)(ImportTable->FirstThunk + (DWORD)ModuleInfoiter->ScanBuffer);
					while (FirstThunk->u1.Function)
					{
						ApiAddress = FirstThunk->u1.Function;
						if (OriThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG32)
						{
							Ordinal = OriThunk->u1.Ordinal & 0x0000FFFF;
							OriApiAddress = MyGetProcAddress(DllName, (char *)Ordinal, &IsApiSet, RealDllName);
							sprintf_s(OrdinalName, 13, "Ordinal:%04x", Ordinal);
							ApiName = OrdinalName;
						}
						else
						{
							ByName = (PIMAGE_IMPORT_BY_NAME)(OriThunk->u1.AddressOfData + (DWORD)ModuleInfoiter->ScanBuffer);
							ApiName = ByName->Name;
							OriApiAddress = MyGetProcAddress(DllName, ApiName, &IsApiSet, RealDllName);
						}
						if (ApiAddress != OriApiAddress)
						{
							Info.HookType = IatHook;
							Info.OriginalAddress = OriApiAddress;
							Info.HookAddress = ApiAddress;
							memset(Info.HookedApiName, 0, 64);
							MultiByteToWideChar(CP_ACP, 0, ApiName, strlen(ApiName) + 1, Info.HookedApiName, 128);
							memset(Info.HookedModule, 0, 64);
							if (IsApiSet)
								wcscpy_s(Info.HookedModule, 64, RealDllName);
							else
								MultiByteToWideChar(CP_ACP, 0, DllName, strlen(DllName) + 1, Info.HookedModule, 64);
							GetModulePathByAddress(ApiAddress, Info.HookLocation);
							HookInfo.push_back(Info);
						}
						++OriThunk;
						++FirstThunk;
					}
					++ImportTable;
				}
			}
		}
		return ret;
	}

	bool  ScanHook::ElevatedPriv()
	{
		HANDLE hToken;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken))
		{
			return 0;
		}
		TOKEN_PRIVILEGES tkp;
		tkp.PrivilegeCount = 1;
		LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &tkp.Privileges[0].Luid);
		tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
		if (!AdjustTokenPrivileges(hToken, FALSE, &tkp, sizeof(tkp), NULL, NULL))
		{
			return 0;
		}
		CloseHandle(hToken);
		return 1;
	}

	bool ScanHook::QuerySystemInfo()
	{
		bool ret = 0;
		PNT_PEB Peb;
		NT_PROCESS_BASIC_INFORMATION BaseInfo;
		if (!NtQueryInformationProcess(GetCurrentProcess(), ProcessBasicInformation, &BaseInfo, sizeof(NT_PROCESS_BASIC_INFORMATION), 0))
		{
			Peb = (PNT_PEB)(BaseInfo.PebBaseAddress);
			MajorVersion = Peb->OSMajorVersion;
			MinorVersion = Peb->OSMinorVersion;
			if (MajorVersion >= 6 && MinorVersion >= 1)
				ApiSetMapHead = (DWORD *)(Peb->ApiSetMap);
			ret = 1;
		}
		return ret;
	}

	bool ScanHook::QueryModuleInfo()
	{
		bool ret = 0;
		DWORD Peb;
		MODULE_INFO Info;
		NT_PROCESS_BASIC_INFORMATION BaseInfo;
		PNT_PEB_LDR_DATA LdrData;
		NT_LDR_DATA_TABLE_ENTRY Buffer;
		PNT_LDR_DATA_TABLE_ENTRY LdrTable, EndLdrTable;
		if (!NtQueryInformationProcess(hProcess, ProcessBasicInformation, &BaseInfo, sizeof(NT_PROCESS_BASIC_INFORMATION), 0))
		{
			Peb = BaseInfo.PebBaseAddress;
			__try
			{
				ReadProcessMemory(hProcess, (void *)(Peb + 0xc), &LdrData, 4, 0);
				ReadProcessMemory(hProcess, &(LdrData->InLoadOrderModuleList), &LdrTable, 4, 0);
				ReadProcessMemory(hProcess, LdrTable, &Buffer, sizeof(NT_LDR_DATA_TABLE_ENTRY), 0);
				EndLdrTable = LdrTable;
				do
				{
					Info.DllBase = (DWORD)Buffer.DllBase;
					Info.SizeOfImage = Buffer.SizeOfImage;
					memset(Info.FullName, 0, 260);
					ReadProcessMemory(hProcess, Buffer.FullDllName.Buffer, Info.FullName, Buffer.FullDllName.Length, 0);
					memset(Info.BaseName, 0, 64);
					ReadProcessMemory(hProcess, Buffer.BaseDllName.Buffer, Info.BaseName, Buffer.BaseDllName.Length, 0);
					Info.ScanBuffer = new BYTE[Buffer.SizeOfImage];
					ReadProcessMemory(hProcess, Buffer.DllBase, Info.ScanBuffer, Buffer.SizeOfImage, 0);
					Info.OrigBuffer = new BYTE[Buffer.SizeOfImage];
					PeLoader(Info.FullName, Info.OrigBuffer, Buffer.SizeOfImage, (DWORD)Buffer.DllBase);
					ModuleInfo.push_back(Info);
					ReadProcessMemory(hProcess, Buffer.InLoadOrderLinks.Flink, &Buffer, sizeof(NT_LDR_DATA_TABLE_ENTRY), 0);
					LdrTable = (PNT_LDR_DATA_TABLE_ENTRY)(Buffer.InLoadOrderLinks.Flink);
				} while (LdrTable != EndLdrTable);
				ret = 1;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				ret = 0;
			}
		}
		return ret;
	}

	bool ScanHook::PeLoader(WCHAR *FilePath, void *BaseAddress, DWORD BufferSize, DWORD DllBase)
	{
		bool ret = 0;
		void *Buffer;
		DWORD SectionNum, HeadSize, DateSize;
		HANDLE hFile;
		PE_INFO Pe;
		PIMAGE_SECTION_HEADER SectionHead;
		if (BaseAddress)
		{
			hFile = CreateFile(FilePath, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
			if (hFile != INVALID_HANDLE_VALUE)
			{
				Buffer = new BYTE[BufferSize];
				if (Buffer)
				{
					if (ReadFile(hFile, Buffer, BufferSize, 0, 0))
					{
						ParsePe((DWORD)Buffer, &Pe);
						SectionHead = IMAGE_FIRST_SECTION(Pe.PeHead);
						SectionNum = Pe.PeHead->FileHeader.NumberOfSections;
						HeadSize = Pe.PeHead->OptionalHeader.SizeOfHeaders;
						memset(BaseAddress, 0, BufferSize);
						memcpy(BaseAddress, Buffer, HeadSize);
						for (DWORD i = 0; i < SectionNum; i++)
						{
							DateSize = SectionHead[i].SizeOfRawData;
							if (DateSize > SectionHead[i].Misc.VirtualSize)
								DateSize = SectionHead[i].Misc.VirtualSize;
							memcpy((void *)((DWORD)BaseAddress + SectionHead[i].VirtualAddress),
								(void *)((DWORD)Buffer + SectionHead[i].PointerToRawData), DateSize);
						}
						FixBaseRelocTable((DWORD)BaseAddress, DllBase);
						ret = 1;
					}
					delete[] Buffer;
				}
				CloseHandle(hFile);
			}
		}
		return ret;
	}

	bool ScanHook::FixBaseRelocTable(DWORD NewImageBase, DWORD ExistImageBase)
	{
		LONGLONG Diff;
		ULONG TotalCountBytes = 0;
		ULONG_PTR VA;
		ULONGLONG OriginalImageBase;
		ULONG SizeOfBlock;
		PUSHORT NextOffset = 0;
		PE_INFO Pe;
		PIMAGE_BASE_RELOCATION NextBlock;
		ParsePe(NewImageBase, &Pe);
		if (Pe.PeHead == 0)
			return 0;
		switch (Pe.PeHead->OptionalHeader.Magic)
		{
		case IMAGE_NT_OPTIONAL_HDR32_MAGIC:
		{
			OriginalImageBase = ((PIMAGE_NT_HEADERS32)Pe.PeHead)->OptionalHeader.ImageBase;
			break;
		}
		case IMAGE_NT_OPTIONAL_HDR64_MAGIC:
		{
			OriginalImageBase = ((PIMAGE_NT_HEADERS64)Pe.PeHead)->OptionalHeader.ImageBase;
			break;
		}
		default:
			return 0;
		}
		NextBlock = (PIMAGE_BASE_RELOCATION)(NewImageBase +
			Pe.PeHead->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);
		TotalCountBytes = (NewImageBase + Pe.PeHead->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size);
		if (!NextBlock || !TotalCountBytes)
		{
			if (Pe.PeHead->FileHeader.Characteristics & IMAGE_FILE_RELOCS_STRIPPED)
				return 0;
			else
				return 1;
		}
		Diff = (ULONG_PTR)ExistImageBase - OriginalImageBase;
		while (TotalCountBytes)
		{
			SizeOfBlock = NextBlock->SizeOfBlock;
			TotalCountBytes -= SizeOfBlock;
			SizeOfBlock -= sizeof(IMAGE_BASE_RELOCATION);
			SizeOfBlock /= sizeof(USHORT);
			NextOffset = (PUSHORT)((PCHAR)NextBlock + sizeof(IMAGE_BASE_RELOCATION));
			VA = (ULONG_PTR)NewImageBase + NextBlock->VirtualAddress;
			NextBlock = ProcessRelocationBlock(VA, SizeOfBlock, NextOffset, Diff);
			if (!NextBlock)
				return 0;
		}
		return 1;
	}

	PIMAGE_BASE_RELOCATION ScanHook::ProcessRelocationBlock(ULONG_PTR VA, ULONG SizeOfBlock, PUSHORT NextOffset, LONGLONG Diff)
	{
		PUCHAR FixupVA;
		USHORT Offset;
		LONG Temp;
		ULONGLONG Value64;
		while (SizeOfBlock--) 
		{
			Offset = *NextOffset & (USHORT)0xfff;
			FixupVA = (PUCHAR)(VA + Offset);
			switch ((*NextOffset) >> 12) 
			{
			case IMAGE_REL_BASED_HIGHLOW:
			{
				*(LONG UNALIGNED *)FixupVA += (ULONG)Diff;
				break;
			}
			case IMAGE_REL_BASED_HIGH:
			{
				Temp = *(PUSHORT)FixupVA & 16;
				Temp += (ULONG)Diff;
				*(PUSHORT)FixupVA = (USHORT)(Temp >> 16);
				break;
			}
			case IMAGE_REL_BASED_HIGHADJ:
			{
				if (Offset & LDRP_RELOCATION_FINAL)
				{
					++NextOffset;
					--SizeOfBlock;
					break;
				}
				Temp = *(PUSHORT)FixupVA & 16;
				++NextOffset;
				--SizeOfBlock;
				Temp += (LONG)(*(PSHORT)NextOffset);
				Temp += (ULONG)Diff;
				Temp += 0x8000;
				*(PUSHORT)FixupVA = (USHORT)(Temp >> 16);
				break;
			}
			case IMAGE_REL_BASED_LOW:
			{
				Temp = *(PSHORT)FixupVA;
				Temp += (ULONG)Diff;
				*(PUSHORT)FixupVA = (USHORT)Temp;
				break;
			}
			case IMAGE_REL_BASED_IA64_IMM64:
			{
				FixupVA = (PUCHAR)((ULONG_PTR)FixupVA & ~(15));
				Value64 = (ULONGLONG)0;
				EXT_IMM64(Value64,
					(PULONG)FixupVA + EMARCH_ENC_I17_IMM7B_INST_WORD_X,
					EMARCH_ENC_I17_IMM7B_SIZE_X,
					EMARCH_ENC_I17_IMM7B_INST_WORD_POS_X,
					EMARCH_ENC_I17_IMM7B_VAL_POS_X);
				EXT_IMM64(Value64,
					(PULONG)FixupVA + EMARCH_ENC_I17_IMM9D_INST_WORD_X,
					EMARCH_ENC_I17_IMM9D_SIZE_X,
					EMARCH_ENC_I17_IMM9D_INST_WORD_POS_X,
					EMARCH_ENC_I17_IMM9D_VAL_POS_X);
				EXT_IMM64(Value64,
					(PULONG)FixupVA + EMARCH_ENC_I17_IMM5C_INST_WORD_X,
					EMARCH_ENC_I17_IMM5C_SIZE_X,
					EMARCH_ENC_I17_IMM5C_INST_WORD_POS_X,
					EMARCH_ENC_I17_IMM5C_VAL_POS_X);
				EXT_IMM64(Value64,
					(PULONG)FixupVA + EMARCH_ENC_I17_IC_INST_WORD_X,
					EMARCH_ENC_I17_IC_SIZE_X,
					EMARCH_ENC_I17_IC_INST_WORD_POS_X,
					EMARCH_ENC_I17_IC_VAL_POS_X);
				EXT_IMM64(Value64,
					(PULONG)FixupVA + EMARCH_ENC_I17_IMM41a_INST_WORD_X,
					EMARCH_ENC_I17_IMM41a_SIZE_X,
					EMARCH_ENC_I17_IMM41a_INST_WORD_POS_X,
					EMARCH_ENC_I17_IMM41a_VAL_POS_X);
				EXT_IMM64(Value64,
					((PULONG)FixupVA + EMARCH_ENC_I17_IMM41b_INST_WORD_X),
					EMARCH_ENC_I17_IMM41b_SIZE_X,
					EMARCH_ENC_I17_IMM41b_INST_WORD_POS_X,
					EMARCH_ENC_I17_IMM41b_VAL_POS_X);
				EXT_IMM64(Value64,
					((PULONG)FixupVA + EMARCH_ENC_I17_IMM41c_INST_WORD_X),
					EMARCH_ENC_I17_IMM41c_SIZE_X,
					EMARCH_ENC_I17_IMM41c_INST_WORD_POS_X,
					EMARCH_ENC_I17_IMM41c_VAL_POS_X);
				EXT_IMM64(Value64,
					((PULONG)FixupVA + EMARCH_ENC_I17_SIGN_INST_WORD_X),
					EMARCH_ENC_I17_SIGN_SIZE_X,
					EMARCH_ENC_I17_SIGN_INST_WORD_POS_X,
					EMARCH_ENC_I17_SIGN_VAL_POS_X);
				Value64 += Diff;
				INS_IMM64(Value64,
					((PULONG)FixupVA + EMARCH_ENC_I17_IMM7B_INST_WORD_X),
					EMARCH_ENC_I17_IMM7B_SIZE_X,
					EMARCH_ENC_I17_IMM7B_INST_WORD_POS_X,
					EMARCH_ENC_I17_IMM7B_VAL_POS_X);
				INS_IMM64(Value64,
					((PULONG)FixupVA + EMARCH_ENC_I17_IMM9D_INST_WORD_X),
					EMARCH_ENC_I17_IMM9D_SIZE_X,
					EMARCH_ENC_I17_IMM9D_INST_WORD_POS_X,
					EMARCH_ENC_I17_IMM9D_VAL_POS_X);
				INS_IMM64(Value64,
					((PULONG)FixupVA + EMARCH_ENC_I17_IMM5C_INST_WORD_X),
					EMARCH_ENC_I17_IMM5C_SIZE_X,
					EMARCH_ENC_I17_IMM5C_INST_WORD_POS_X,
					EMARCH_ENC_I17_IMM5C_VAL_POS_X);
				INS_IMM64(Value64,
					((PULONG)FixupVA + EMARCH_ENC_I17_IC_INST_WORD_X),
					EMARCH_ENC_I17_IC_SIZE_X,
					EMARCH_ENC_I17_IC_INST_WORD_POS_X,
					EMARCH_ENC_I17_IC_VAL_POS_X);
				INS_IMM64(Value64,
					((PULONG)FixupVA + EMARCH_ENC_I17_IMM41a_INST_WORD_X),
					EMARCH_ENC_I17_IMM41a_SIZE_X,
					EMARCH_ENC_I17_IMM41a_INST_WORD_POS_X,
					EMARCH_ENC_I17_IMM41a_VAL_POS_X);
				INS_IMM64(Value64,
					((PULONG)FixupVA + EMARCH_ENC_I17_IMM41b_INST_WORD_X),
					EMARCH_ENC_I17_IMM41b_SIZE_X,
					EMARCH_ENC_I17_IMM41b_INST_WORD_POS_X,
					EMARCH_ENC_I17_IMM41b_VAL_POS_X);
				INS_IMM64(Value64,
					((PULONG)FixupVA + EMARCH_ENC_I17_IMM41c_INST_WORD_X),
					EMARCH_ENC_I17_IMM41c_SIZE_X,
					EMARCH_ENC_I17_IMM41c_INST_WORD_POS_X,
					EMARCH_ENC_I17_IMM41c_VAL_POS_X);
				INS_IMM64(Value64,
					((PULONG)FixupVA + EMARCH_ENC_I17_SIGN_INST_WORD_X),
					EMARCH_ENC_I17_SIGN_SIZE_X,
					EMARCH_ENC_I17_SIGN_INST_WORD_POS_X,
					EMARCH_ENC_I17_SIGN_VAL_POS_X);
				break;
			}
			case IMAGE_REL_BASED_DIR64:
			{
				*(ULONGLONG UNALIGNED *)FixupVA += Diff;
				break;
			}
			case IMAGE_REL_BASED_MIPS_JMPADDR:
			{
				Temp = (*(PULONG)FixupVA & 0x3ffffff) & 2;
				Temp += (ULONG)Diff;
				*(PULONG)FixupVA = (*(PULONG)FixupVA & ~0x3ffffff) | ((Temp >> 2) & 0x3ffffff);
				break;
			}
			case IMAGE_REL_BASED_ABSOLUTE: 
				break;
			default:
				return (PIMAGE_BASE_RELOCATION)NULL;
			}
			++NextOffset;
		}
		return (PIMAGE_BASE_RELOCATION)NextOffset;
	}

	bool ScanHook::IsGlobalVar(PIMAGE_NT_HEADERS PeHead, DWORD Rva)
	{
		//DWORD Offset;
		WORD SectionNum;
		PIMAGE_SECTION_HEADER Section;
		SectionNum = PeHead->FileHeader.NumberOfSections;
		Section = IMAGE_FIRST_SECTION(PeHead);
		for (int i = 0; i < SectionNum; ++i)
		{
			if ((Section->VirtualAddress <= Rva) && (Rva < (Section->SizeOfRawData + Section->VirtualAddress)))
				return 0;
			++Section;
		}
		return 1;
	}

	bool ScanHook::ParsePe(DWORD ImageBase, PPE_INFO Pe)
	{
		bool ret = 0;
		PIMAGE_DOS_HEADER DosHead;
		PIMAGE_OPTIONAL_HEADER OpitionHead;
		if (ImageBase)
		{
			DosHead = (PIMAGE_DOS_HEADER)ImageBase;
			if (DosHead->e_magic ==IMAGE_DOS_SIGNATURE)
			{
				Pe->PeHead = (PIMAGE_NT_HEADERS)(ImageBase + DosHead->e_lfanew);
				if (Pe->PeHead->Signature == IMAGE_NT_SIGNATURE)
				{
					OpitionHead = &(Pe->PeHead->OptionalHeader);
					Pe->ExportTableRva = OpitionHead->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
					Pe->ExportSize = OpitionHead->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
					Pe->ImportTableRva = OpitionHead->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
					Pe->ImportSize = OpitionHead->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size;
					ret = 1;
				}
			}
		}
		return ret;
	}

	DWORD ScanHook::GetExportByOrdinal(DWORD ImageBase, WORD Ordinal)
	{
		DWORD ApiAddress = 0;
		DWORD *Eat;
		PE_INFO Pe;
		PIMAGE_EXPORT_DIRECTORY ExportTable;
		ParsePe(ImageBase, &Pe);
		if (Pe.ExportSize)
		{
			ExportTable = (PIMAGE_EXPORT_DIRECTORY)(ImageBase + Pe.ExportTableRva);
			Eat = (DWORD *)(ImageBase + ExportTable->AddressOfFunctions);
			ApiAddress = ((Eat[Ordinal - ExportTable->Base] != 0) ? (ImageBase + Eat[Ordinal - ExportTable->Base]) : 0);
			if ((ApiAddress >= (DWORD)ExportTable) &&
				(ApiAddress < ((DWORD)ExportTable + Pe.ExportSize)))
				ApiAddress = FileNameRedirection(ImageBase, (char *)ApiAddress);
		}
		return ApiAddress;
	}

	DWORD ScanHook::GetExportByName(DWORD ImageBase, char *ProcName)
	{
		int cmp;
		char *ApiName;
		DWORD ApiAddress = 0;
		WORD Ordinal, *NameOrd;
		DWORD *Ent, *Eat, HigthIndex, LowIndex = 0, MidIndex;
		PE_INFO Pe;
		PIMAGE_EXPORT_DIRECTORY ExportTable;
		ParsePe(ImageBase, &Pe);
		if (Pe.ExportSize)
		{
			ExportTable = (PIMAGE_EXPORT_DIRECTORY)(ImageBase + Pe.ExportTableRva);
			Eat = (DWORD *)(ImageBase + ExportTable->AddressOfFunctions);
			Ent = (DWORD *)(ImageBase + ExportTable->AddressOfNames);
			NameOrd = (WORD *)(ImageBase + ExportTable->AddressOfNameOrdinals);
			HigthIndex = ExportTable->NumberOfNames;
			__try
			{
				while (LowIndex <= HigthIndex)
				{
					MidIndex = (LowIndex + HigthIndex) / 2;
					ApiName = (char *)(ImageBase + Ent[MidIndex]);
					cmp = strcmp(ProcName, ApiName);
					if (cmp < 0)
					{
						HigthIndex = MidIndex - 1;
						continue;
					}
					if (cmp > 0)
					{
						LowIndex = MidIndex + 1;
						continue;
					}
					if (cmp == 0)
					{
						Ordinal = NameOrd[MidIndex];
						break;
					}
				}
				if (LowIndex > HigthIndex)
					return 0;
				ApiAddress = (ImageBase + Eat[Ordinal]);
				if (ApiAddress >= (DWORD)ExportTable &&
					(ApiAddress < ((DWORD)ExportTable + Pe.ExportSize)))
				{
					ApiAddress = FileNameRedirection(ImageBase, (char *)ApiAddress);
					IsRedirction = 1;
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return 0;
			}
		}
		return ApiAddress;
	}

	DWORD ScanHook::FileNameRedirection(DWORD ImageBase, char *RedirectionName)
	{
		char *ptr, *ProcName;
		char Buffer[128];
		WORD Oridnal;
		WCHAR DllName[128];
		DWORD ApiAddress = 0;
		strcpy_s(Buffer, 128, RedirectionName);
		ptr = strchr(Buffer, '.');
		if (ptr)
		{
			*ptr = 0;
			MultiByteToWideChar(CP_ACP, 0, Buffer, sizeof(Buffer), DllName, 128);
			if (!_wcsnicmp(DllName, L"api-", 4))
			{
				IsFromRedirction = 1;
				ResolveApiSet(DllName, DllName, 128);
				IsFromRedirction = 0;
				goto get_api_address;
			}
			else
			{
			get_api_address:
				ImageBase = (DWORD)LoadLibraryW(DllName);
				if (ImageBase)
				{
					if (*(char *)(ptr + 1) == '#')
					{
						Oridnal = (WORD)strtoul((char *)(ptr + 2), 0, 10);
						ApiAddress = GetExportByOrdinal(ImageBase, Oridnal);
					}
					else
					{
						ProcName = (char *)(ptr + 1);
						ApiAddress = GetExportByName(ImageBase, ProcName);
					}
				}
			}
		}
		return ApiAddress;
	}

	bool ScanHook::ResolveApiSet(WCHAR *ApiSetName, WCHAR *HostName, DWORD Size)
	{
		bool ret = 0;
		WCHAR *NameBuffer, *ptr;
		WCHAR LibName[64];
		DWORD ApiAddress = 0, LibNameSize, HostNameSize;
		DWORD *Version;;
		PNT_API_SET_NAMESPACE_ARRAY_V2 SetMapHead_v2;
		PNT_API_SET_VALUE_ARRAY_V2 SetMapHost_v2;
		PNT_API_SET_NAMESPACE_ARRAY_V4 SetMapHead_v4;
		PNT_API_SET_VALUE_ARRAY_V4 SetMapHost_v4;
		Version = ApiSetMapHead;
		ptr = wcschr(ApiSetName, L'.');
		if (ptr)
			*ptr = 0;
		if (Version)
		{
			switch (*Version)
			{
			case 2:
			{
					  SetMapHead_v2 = (PNT_API_SET_NAMESPACE_ARRAY_V2)Version;
					  for (DWORD i = 0; i < SetMapHead_v2->Count; i++)
					  {
						  NameBuffer = (WCHAR *)((DWORD)SetMapHead_v2 + SetMapHead_v2->Entry[i].NameOffset);
						  LibNameSize = SetMapHead_v2->Entry[i].NameLength;
						  wcsncpy_s(LibName, 64, NameBuffer, LibNameSize / sizeof(WCHAR));
						  if (!_wcsicmp((WCHAR *)(ApiSetName + 4), LibName))
						  {
							  SetMapHost_v2 = (PNT_API_SET_VALUE_ARRAY_V2)((DWORD)SetMapHead_v2 + SetMapHead_v2->Entry[i].DataOffset);
							  if (SetMapHost_v2->Count == 1)
							  {
								  HostNameSize = SetMapHost_v2->Entry[0].ValueLength;
								  NameBuffer = (WCHAR *)((DWORD)SetMapHead_v2 + SetMapHost_v2->Entry[0].ValueOffset);
							  }
							  else
							  {
								  HostNameSize = SetMapHost_v2->Entry[0].ValueLength;
								  NameBuffer = (WCHAR *)((DWORD)SetMapHead_v2 + SetMapHost_v2->Entry[0].ValueOffset);
								  if (!_wcsnicmp(ModuleInfoiter->BaseName, NameBuffer, HostNameSize / sizeof(WCHAR)) || IsFromRedirction)
								  {
									  HostNameSize = SetMapHost_v2->Entry[1].ValueLength;
									  NameBuffer = (WCHAR *)((DWORD)SetMapHead_v2 + SetMapHost_v2->Entry[1].ValueOffset);
								  }								  
							  }
							  wcsncpy_s(HostName, Size, NameBuffer, HostNameSize / sizeof(WCHAR));
							  ret = 1;
							  break;
						  }
					  }
			}
			case 4:
			{
					  SetMapHead_v4 = (PNT_API_SET_NAMESPACE_ARRAY_V4)Version;
					  for (DWORD i = 0; i < SetMapHead_v4->Count; i++)
					  {
						  NameBuffer = (WCHAR *)((DWORD)SetMapHead_v4 + SetMapHead_v4->Entry[i].NameOffset);
						  LibNameSize = SetMapHead_v4->Entry[i].NameLength;
						  wcsncpy_s(LibName, 64, NameBuffer, LibNameSize / sizeof(WCHAR));
						  if (!_wcsicmp((WCHAR *)(ApiSetName + 4), LibName))
						  {
							  SetMapHost_v4 = (PNT_API_SET_VALUE_ARRAY_V4)((DWORD)SetMapHead_v4 + SetMapHead_v4->Entry[i].DataOffset);
							  if (SetMapHost_v4->Count == 1)
							  {
								  HostNameSize = SetMapHost_v4->Entry[0].ValueLength;
								  NameBuffer = (WCHAR *)((DWORD)SetMapHead_v4 + SetMapHost_v4->Entry[0].ValueOffset);
							  }
							  else
							  {
								  HostNameSize = SetMapHost_v4->Entry[0].ValueLength;
								  NameBuffer = (WCHAR *)((DWORD)SetMapHead_v4 + SetMapHost_v4->Entry[0].ValueOffset);
								  if (!_wcsnicmp(ModuleInfoiter->BaseName, NameBuffer, HostNameSize / sizeof(WCHAR)) || IsFromRedirction)
								  {
									  HostNameSize = SetMapHost_v4->Entry[1].ValueLength;
									  NameBuffer = (WCHAR *)((DWORD)SetMapHead_v4 + SetMapHost_v4->Entry[1].ValueOffset);
								  }								  
							  }
							  wcsncpy_s(HostName, Size, NameBuffer, HostNameSize / sizeof(WCHAR));
							  ret = 1;
							  break;
						  }
					  }
			}
			default:
				break;
			}
		}
		return ret;
	}

	DWORD ScanHook::MyGetProcAddress(char *DllName, char *ApiName, bool *IsApiSet, WCHAR *RealDllName)
	{
		DWORD ApiAddress = 0;
		WCHAR NameBuffer[64], HostName[64];
		vector<MODULE_INFO>::iterator iter;
		*IsApiSet = 0;
		MultiByteToWideChar(CP_ACP, 0, DllName, strlen(DllName) + 1, NameBuffer, 64);
		if (HIWORD((DWORD)ApiName))
		{
			if (GetModuleInfomation(NameBuffer, iter))
			{
				ApiAddress = GetExportByName((DWORD)iter->OrigBuffer, ApiName);
				if (!IsRedirction)
					ApiAddress = ApiAddress - (DWORD)iter->OrigBuffer + iter->DllBase;
				IsRedirction = 0;
			}
			else
			{
				if (!_wcsnicmp(NameBuffer, L"api-", 4) && (MajorVersion >= 6 && MinorVersion >= 1))
				{
					if (ResolveApiSet(NameBuffer, HostName, 64))
					{
						*IsApiSet = 1;
						wcscpy_s(RealDllName, 64, HostName);
						if (GetModuleInfomation(HostName, iter))
						{
							ApiAddress = GetExportByName((DWORD)iter->OrigBuffer, ApiName);
							if (!IsRedirction)
								ApiAddress = ApiAddress - (DWORD)iter->OrigBuffer + iter->DllBase;
							IsRedirction = 0;
						}
					}
				}
			}
		}
		else
		{
			if (GetModuleInfomation(NameBuffer, iter))
			{
				ApiAddress = GetExportByOrdinal((DWORD)iter->OrigBuffer, (WORD)ApiName);
				ApiAddress = ApiAddress - (DWORD)iter->OrigBuffer + iter->DllBase;
			}
		}
		return ApiAddress;
	}

	bool ScanHook::GetModuleInfomation(WCHAR *DllName, vector<MODULE_INFO>::iterator &iter)
	{
		bool ret = 0;
		for (iter = ModuleInfo.begin(); iter != ModuleInfo.end(); ++iter)
		{
			if (!_wcsicmp(iter->BaseName, DllName))
			{
				ret = 1;
				break;
			}
		}
		return ret;
	}

	bool ScanHook::GetModuleInfomation(DWORD Address, vector<MODULE_INFO>::iterator &iter)
	{
		bool ret = 0;
		DWORD Buffer;
		Address &= 0xFFFF0000;
		while (Address)
		{
			if (ReadProcessMemory(hProcess, (void *)Address, &Buffer, 4, 0))
				if ((WORD)Buffer == IMAGE_DOS_SIGNATURE)
					if (ReadProcessMemory(hProcess, (void *)(Address + 0x3C), &Buffer, 4, 0))
						if (ReadProcessMemory(hProcess, (void *)(Buffer + Address), &Buffer, 4, 0))
							if (Buffer == IMAGE_NT_SIGNATURE)
								break;
			Address -= 0x10000;
		}
		for (iter = ModuleInfo.begin(); iter != ModuleInfo.end(); ++iter)
		{
			if (Address == iter->DllBase)
			{
				ret = 1;
				break;
			}
		}
		return ret;
	}

	void ScanHook::GetModulePath(DWORD Address, WCHAR *ModulePath)
	{
		vector<MODULE_INFO>::iterator iter;
		for (iter = ModuleInfo.begin(); iter != ModuleInfo.end(); ++iter)
		{
			if (iter->DllBase == Address)
				wcscpy_s(ModulePath, 260, iter->FullName);
		}
	}

	void ScanHook::GetModulePathByAddress(DWORD Address, WCHAR *ModulePath)
	{
		DWORD Buffer;
		if (Address)
		{
			memset(ModulePath, 0, 260);
			Address &= 0xFFFF0000;
			while (Address)
			{
				__try
				{
					if (ReadProcessMemory(hProcess, (void *)Address, &Buffer, 4, 0))
						if ((WORD)Buffer == IMAGE_DOS_SIGNATURE)
							if (ReadProcessMemory(hProcess, (void *)(Address + 0x3C), &Buffer, 4, 0))
								if (ReadProcessMemory(hProcess, (void *)(Buffer + Address), &Buffer, 4, 0))
									if (Buffer == IMAGE_NT_SIGNATURE)
										break;
					Address -= 0x10000;
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
					Address = 0;
					break;
				}
			}
			if (Address)
				GetModulePath(Address, ModulePath);
		}
	}
}