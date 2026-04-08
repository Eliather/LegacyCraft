#pragma once
#include "DLCManager.h"

class DLCPack;

class DLCFile
{
protected:
	DLCManager::EDLCType m_type;
	wstring m_path;
	DWORD m_dwSkinId;
	DLCPack *m_parentPack;

public:
	DLCFile(DLCManager::EDLCType type, const wstring &path);
	virtual ~DLCFile() {}

	DLCManager::EDLCType getType()	{ return m_type; }
	wstring getPath()				{ return m_path; }
	DWORD getSkinID()				{ return m_dwSkinId; }
	void setParentPack(DLCPack *parentPack) { m_parentPack = parentPack; }
	DLCPack *getParentPack() { return m_parentPack; }

	virtual void addData(PBYTE pbData, DWORD dwBytes) {}
	virtual PBYTE getData(DWORD &dwBytes) { dwBytes = 0; return NULL; }
	virtual void addParameter(DLCManager::EDLCParameterType type, const wstring &value) {}

	virtual wstring getParameterAsString(DLCManager::EDLCParameterType type) { return L""; }
	virtual bool getParameterAsBool(DLCManager::EDLCParameterType type) { return false;}
};
