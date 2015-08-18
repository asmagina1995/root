#include "TVirtualObject.h"
#include "TDataMember.h"
#include "TError.h"
#include "TListOfDataMembers.h"
#include "TSchemaRule.h"
#include "TSchemaRuleSet.h"
#include "TVirtualCollectionProxy.h"

ClassImp(TVirtualObject)

//______________________________________________________________________________
Bool_t TVirtualObject::IsCollection() const
{
   return fClass->GetCollectionProxy();
}

//______________________________________________________________________________
Int_t TVirtualObject::GetSize() const
{
   if (!IsCollection())
      return 0;
   return fClass->GetCollectionProxy()->Size();
}

//______________________________________________________________________________
TVirtualObject *TVirtualObject::At(Int_t i) const
{
   if (!IsCollection())
      return 0;
   return 0;
}

//______________________________________________________________________________
TVirtualObject *TVirtualObject::GetMember(Int_t id) const
{
   std::map<Int_t, TDictionary::DeclId_t>::const_iterator it = fIds.find(id);
   if (it == fIds.end()) {
      ::Error("TVirtualObject::GetMember", "Cannot find data member with id %d for class %s.", 
              id, GetClass()->GetName());
      return 0;
   }
   TListOfDataMembers* list = (TListOfDataMembers*)(GetClass()->GetListOfDataMembers());
   TDataMember* dm = (TDataMember*)list->Get(it->second);
   TVirtualObject* obj = new TVirtualObject(0);
   obj->fClass  = dm->GetClass(); 
   obj->fObject = (void*)((char*)fObject + dm->GetOffset());
   return obj;
}

//______________________________________________________________________________
template<typename T>
T *TVirtualObject::GetMember(Int_t id) const
{
   std::map<Int_t, TDictionary::DeclId_t>::const_iterator it = fIds.find(id);
   if (it == fIds.end()) {
      ::Error("TVirtualObject::GetMember", "Cannot find data member with id %d for class %s.", 
              id, GetClass()->GetName());
      return 0;
   }
   TListOfDataMembers* list = (TListOfDataMembers*)(GetClass()->GetListOfDataMembers());
   TDataMember* dm = (TDataMember*)list->Get(it->second);
   return (T*)((char*)fObject + dm->GetOffset());
}

// Template instantiations
template double* TVirtualObject::GetMember(Int_t id) const;
template long double* TVirtualObject::GetMember(Int_t id) const;
template long long* TVirtualObject::GetMember(Int_t id) const;

//______________________________________________________________________________
UInt_t TVirtualObject::GetId(TString name)
{
   TDataMember* dm = GetClass()->GetDataMember(name.Data());
   if (!dm) {
      ::Error("TVirtualObject::GetId", "Cannot find data member %s for class %s.", name.Data(), GetClass()->GetName());
      return 0;
   }
   Int_t id = name.Hash();
   fIds.insert(std::pair<Int_t, TDictionary::DeclId_t>(id, dm->GetDeclId()));
   return id;
}

//______________________________________________________________________________
Bool_t TVirtualObject::Load(void *address)
{
   TObjArrayIter it( GetClass()->GetSchemaRules()->FindRules(GetClass()->GetName()) );
   TObject *obj;
   ROOT::TSchemaRule *r;
   while ( (obj = it.Next()) ) {
      r = (ROOT::TSchemaRule*)obj; 
      if (r->GetRuleType() == ROOT::TSchemaRule::kReadRule) {
         ROOT::TSchemaRule::ReadFuncPtr_t func = r->GetReadFunctionPointer();
         func((char*)address, this);
      }
   }
   return kTRUE;
}

//______________________________________________________________________________
Int_t TVirtualObject::GetClassVersion() const
{
   return GetClass()->GetClassVersion();
}
