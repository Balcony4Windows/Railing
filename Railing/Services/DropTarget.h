#pragma once
#include <Windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <functional>
#include <string>

class DropTarget : public IDropTarget {
public:
	using Callback = std::function<void(const std::wstring &)>;

	DropTarget(Callback onDrop) : m_refCount(1), m_onDrop(onDrop) {}

	HRESULT __stdcall QueryInterface(REFIID riid, void **ppvObject) override {
		if (riid == IID_IDropTarget || riid == IID_IUnknown) {
			*ppvObject = this;
			AddRef();
			return S_OK;
		}
		*ppvObject = NULL;
		return E_NOINTERFACE;
	}
	ULONG __stdcall AddRef() override { return InterlockedIncrement(&m_refCount); }
	ULONG __stdcall Release() override {
		LONG count = InterlockedDecrement(&m_refCount);
		if (count == 0) delete this;
		return count;
	}

	HRESULT __stdcall DragEnter(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override {
		FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
		if (pDataObj->QueryGetData(&fmt) == S_OK) {
			*pdwEffect = DROPEFFECT_COPY | DROPEFFECT_LINK; // Allow Copy or Link
			return S_OK;
		}
		*pdwEffect = DROPEFFECT_NONE;
		return S_FALSE;
	}
	HRESULT __stdcall DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override {
		*pdwEffect = DROPEFFECT_COPY | DROPEFFECT_LINK;
		return S_OK;
	}
	HRESULT __stdcall DragLeave() override { return S_OK; }

	HRESULT __stdcall Drop(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override {
		FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
		STGMEDIUM stg;
		if (SUCCEEDED(pDataObj->GetData(&fmt, &stg))) {
			HDROP hDrop = (HDROP)stg.hGlobal;
			UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);

			if (count > 0) {
				wchar_t path[MAX_PATH];
				if (DragQueryFileW(hDrop, 0, path, MAX_PATH) && m_onDrop) m_onDrop(path);
			}
			ReleaseStgMedium(&stg);
		}
		*pdwEffect = DROPEFFECT_LINK;
		return S_OK;
	}
private:
	LONG m_refCount;
	Callback m_onDrop;
};