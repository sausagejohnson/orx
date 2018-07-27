#ifndef __[name]_H_
#define __[name]_H_

//! Includes
// The following define skips compilation of ScrollEd (map editor) for now
#define __NO_SCROLLED__
#include "Scroll.h"

//! [name] class
class [name] : public Scroll<[name]>
{
public:

private:
	virtual void BindObjects();
	//! Initialize the program
	virtual orxSTATUS Init();
	//! Callback called every frame
	virtual orxSTATUS Run();
	//! Exit the program
	virtual void      Exit();

	virtual orxSTATUS Bootstrap() const;
};

#endif // __[name]_H_