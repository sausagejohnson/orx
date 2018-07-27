#include "[name]Object.h"

void [name]Object::OnCreate()
{
}

void [name]Object::OnDelete()
{
	// Do nothing when deleted
}


void [name]Object::Update(const orxCLOCK_INFO &_rstInfo)
{
}

orxBOOL [name]Object::OnCollide(ScrollObject *_poCollider,
	const orxSTRING _zPartName,
	const orxSTRING _zColliderPartName,
	const orxVECTOR &_rvPosition,
	const orxVECTOR &_rvNormal)
{
	if (_poCollider == orxNULL) {
		return orxTRUE;
	}

	return orxTRUE;
}