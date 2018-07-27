
#define __SCROLL_IMPL__
#include "[name].h"
#include "[name]Object.h"
#undef __SCROLL_IMPL__


/*
* This is a basic C++ template to quickly and easily get started with a project or tutorial.
*/

void [name]::BindObjects()
{
	ScrollBindObject<[name]Object>("[name]Object");
}

orxSTATUS [name]::Init()
{
	orxSTATUS result = orxSTATUS_SUCCESS;

	ScrollObject *scrollObject = CreateObject("[name]Object");

	return result;
}

/** Run function, is called every clock cycle
*/
orxSTATUS [name]::Run()
{
	orxSTATUS eResult = orxSTATUS_SUCCESS;

	/* Should quit? */
	if (orxInput_IsActive("Quit"))
	{
		/* Updates result */
		eResult = orxSTATUS_FAILURE;
	}

	/* Done! */
	return eResult;
}

/** Exit function
*/
void [name]::Exit()
{
}

/** Locate the starting config .ini file
*/
orxSTATUS [name]::Bootstrap() const
{
	// Add to the list of locations that config files can be loaded from
	orxResource_AddStorage(orxCONFIG_KZ_RESOURCE_GROUP, "../data/config", orxFALSE);

	//Locate the [name].ini, [name]d.ini or [name]p.ini file, depending on your exe.
	return orxSTATUS_SUCCESS;
}

/** Main function
*/
int main(int argc, char **argv)
{

	// Executes game
	[name]::GetInstance().Execute(argc, argv);

	// Done!
	return EXIT_SUCCESS;
}

