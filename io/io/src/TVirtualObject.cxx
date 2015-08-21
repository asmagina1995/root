#include "TVirtualObject.h"
#include "TDataMember.h"
#include "TError.h"
#include "TListOfDataMembers.h"
#include "TSchemaRule.h"
#include "TSchemaRuleSet.h"
#include "TVirtualCollectionProxy.h"

#include <iostream>

ClassImp(TVirtualObject)

//______________________________________________________________________________
Bool_t TVirtualObject::IsCollection() const
{
   return fClass ? GetClass()->GetCollectionProxy() : 0;
}

//______________________________________________________________________________
Int_t TVirtualObject::GetSize() const
{
   if (!IsCollection())
      return 0;
   GetClass()->GetCollectionProxy()->PushProxy(fObject);
   Int_t size = GetClass()->GetCollectionProxy()->Size(); 
   GetClass()->GetCollectionProxy()->PopProxy();
   return size;
}

//______________________________________________________________________________
TVirtualObject *TVirtualObject::At(Int_t i) const
{
   if (!IsCollection())
      return 0;

   TVirtualObject* obj = new TVirtualObject();
   obj->fClass  = GetClass()->GetCollectionProxy()->GetValueClass();
   
   GetClass()->GetCollectionProxy()->PushProxy(fObject);
   obj->fObject = GetClass()->GetCollectionProxy()->At(i);
   GetClass()->GetCollectionProxy()->PopProxy();
   return obj;
}

//______________________________________________________________________________
TVirtualObject *TVirtualObject::GetMember(Int_t id) const
{
   if (!fObject)
      return 0;

   std::map<UInt_t, TDictionary::DeclId_t>::const_iterator it = fIds.find(id);
   if (it == fIds.end()) {
      ::Error("TVirtualObject::GetMember", "Cannot find data member with id %d for class %s.", 
              id, GetClass()->GetName());
      return 0;
   }
   TListOfDataMembers* list = (TListOfDataMembers*)(GetClass()->GetListOfDataMembers());
   TDataMember* dm = (TDataMember*)list->Get(it->second);
   TVirtualObject* obj;
   if (!dm->GetArrayDim()) {
      obj = new TVirtualObject(); 
      obj->fClass  = dm->GetClass(); 
      obj->fObject = (void*)((char*)fObject + dm->GetOffset());
   }
   else { 
      obj = new TVirtualObject[dm->GetArrayDim()];
      for (Int_t i = 0; i < dm->GetArrayDim(); i++) {
         obj[i].fClass = dm->GetClass();
         obj[i].fObject = (void*)((char*)fObject + dm->GetOffset() + dm->GetUnitSize() * i);
      }
   }
   return obj;
}

//______________________________________________________________________________
template<typename T>
T TVirtualObject::GetMember(Int_t id) const
{
   if (!fClass)
      return 0;

   std::map<UInt_t, TDictionary::DeclId_t>::const_iterator it = fIds.find(id);
   if (it == fIds.end()) {
      ::Error("TVirtualObject::GetMember", "Cannot find data member with id %d for class %s.", 
              id, GetClass()->GetName());
      return 0;
   }
   TListOfDataMembers* list = (TListOfDataMembers*)(GetClass()->GetListOfDataMembers());
   TDataMember* dm = (TDataMember*)list->Get(it->second);
   return *(T*)((char*)fObject + dm->GetOffset());
}

// Template instantiations
template bool TVirtualObject::GetMember<bool> (Int_t id) const;
template char TVirtualObject::GetMember<char> (Int_t id) const;
template char* TVirtualObject::GetMember<char*> (Int_t id) const;
template double TVirtualObject::GetMember<double> (Int_t id) const;
template float TVirtualObject::GetMember<float> (Int_t id) const;
template int TVirtualObject::GetMember<int> (Int_t id) const;
template long TVirtualObject::GetMember<long> (Int_t id) const;
template long long TVirtualObject::GetMember<long long> (Int_t id) const;
template short TVirtualObject::GetMember<short> (Int_t id) const;
template signed char TVirtualObject::GetMember<signed char> (Int_t id) const;
template unsigned TVirtualObject::GetMember<unsigned> (Int_t id) const;
template unsigned char TVirtualObject::GetMember<unsigned char> (Int_t id) const;
template unsigned long TVirtualObject::GetMember<unsigned long> (Int_t id) const;
template unsigned long long TVirtualObject::GetMember<unsigned long long> (Int_t id) const;
template unsigned short TVirtualObject::GetMember<unsigned short> (Int_t id) const;

template<>
TVirtualObject* TVirtualObject::GetMember<TVirtualObject*> (Int_t id) const 
{ 
   return GetMember(id); 
};

//______________________________________________________________________________
UInt_t TVirtualObject::GetId(const char* name)
{
   TDataMember* dm = GetClass()->GetDataMember(name);
   if (!dm) {
      ::Error("TVirtualObject::GetId", "Cannot find data member %s for class %s.", name, GetClass()->GetName());
      return 0;
   }
   UInt_t id = TString(name).Hash();
   fIds.insert(std::pair<UInt_t, TDictionary::DeclId_t>(id, dm->GetDeclId()));
   return id;
}

//______________________________________________________________________________
Bool_t TVirtualObject::Load(void *address, const char* classname)
{
   if (!GetClass())
      return kFALSE;

   TClass* target = TClass::GetClass(classname);
   if (!target)
      return kFALSE;

   TObjArrayIter it(target->GetSchemaRules()->FindRules(GetClass()->GetName()));
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
