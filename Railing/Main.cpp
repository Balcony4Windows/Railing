#include "Railing.h"

int WINAPI WinMain(__in HINSTANCE hInstance, __in_opt HINSTANCE, __in LPSTR, __in int nCmdShow)
{

	Railing railing;
	if (!railing.Initialize(hInstance)) return 0;

	railing.RunMessageLoop();
	return 0;
}