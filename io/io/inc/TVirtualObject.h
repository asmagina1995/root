// @(#)root/io:$Id$
// Author: Philippe Canal July, 2008

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
  *************************************************************************/

#ifndef ROOT_TVirtualObject
#define ROOT_TVirtualObject


//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TVirtualObject                                                       //
//                                                                      //
// Wrapper around an object and giving indirect access to its content    //
// even if the object is not of a class in the Cint/Reflex dictionary.  //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#ifndef ROOT_TClassRef
#include "TClassRef.h"
#endif

#ifndef ROOT_TDictionary
#include "TDictionary.h"
#endif

#include <map>

class TVirtualObject {
private:
   std::map<Int_t, TDictionary::DeclId_t> fIds;

   TVirtualObject(const TVirtualObject&);             // not implemented
   TVirtualObject &operator=(const TVirtualObject&);  // not implemented

public:
   TClassRef  fClass;
   void      *fObject;

   TVirtualObject(TClass *cl) : fClass(cl), fObject(cl ? cl->New() : 0) { }
   ~TVirtualObject() { if (fClass) fClass->Destructor(fObject); }

   TClass  *GetClass() const { return fClass; }
   void    *GetObject() const { return fObject; }
   
   Bool_t                  IsCollection() const;
   Int_t                   GetSize() const;
   TVirtualObject         *At(Int_t i) const;
   TVirtualObject         *GetMember(Int_t id) const;
   template<typename T> T  GetMember(Int_t id) const;
   UInt_t                  GetId(TString name);
   Bool_t                  Load(void *address);
   Int_t                   GetClassVersion() const;
};

#endif // ROOT_TVirtualObject
