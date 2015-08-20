// @(#)root/core:$Id$
// author: Lukasz Janyst <ljanyst@cern.ch>

#include "TSchemaRuleSet.h"
#include "TSchemaRule.h"
#include "TObjArray.h"
#include "TObjString.h"
#include "TClass.h"
#include "TROOT.h"
#include "Riostream.h"

#include "TVirtualCollectionProxy.h"
#include "TVirtualStreamerInfo.h"
#include "TVirtualMutex.h"
#include "TInterpreter.h" // For gInterpreterMutex
#include "TStreamerElement.h"
#include "TClassEdit.h"

ClassImp(TSchemaRule)

#include <iostream>

using namespace ROOT;

//------------------------------------------------------------------------------
TSchemaRuleSet::TSchemaRuleSet(): fPersistentRules( 0 ), fRemainingRules( 0 ),
                                  fAllRules( 0 ), fVersion(-3), fCheckSum( 0 )
{
   // Default constructor.

   fPersistentRules = new TObjArray();
   fRemainingRules  = new TObjArray();
   fAllRules        = new TObjArray();
   fAllRules->SetOwner( kTRUE );

   fSourceTargets = new RuleMap_t();
}

//------------------------------------------------------------------------------
TSchemaRuleSet::~TSchemaRuleSet()
{
   // Destructor.

   delete fPersistentRules;
   delete fRemainingRules;
   delete fAllRules;
   delete fSourceTargets;
}

//------------------------------------------------------------------------------
void TSchemaRuleSet::ls(Option_t *) const
{
   // The ls function lists the contents of a class on stdout. Ls output
   // is typically much less verbose then Dump().

   TROOT::IndentLevel();
   std::cout << "TSchemaRuleSet for " << fClassName << ":\n";
   TROOT::IncreaseDirLevel();
   TObject *object = 0;
   TIter next(fPersistentRules);
   while ((object = next())) {
      object->ls(fClassName);
   }
   TROOT::DecreaseDirLevel();
}

//------------------------------------------------------------------------------
Bool_t TSchemaRuleSet::AddRules( TSchemaRuleSet* /* rules */, EConsistencyCheck /* checkConsistency */, TString * /* errmsg */ )
{
   return kFALSE;
}

//------------------------------------------------------------------------------
Bool_t TSchemaRuleSet::AddRule( TSchemaRule* rule, EConsistencyCheck checkConsistency, TString *errmsg )
{
   // The consistency check always fails if the TClass object was not set!
   // if checkConsistency is:
   //   kNoCheck: no check is done, register the rule as is
   //   kCheckConflict: check only for conflicting rules
   //   kCheckAll: check for conflict and check for rule about members that are not in the current class layout.
   // return kTRUE if the layout is accepted, in which case we take ownership of
   // the rule object.
   // return kFALSE if the rule failed one of the test, the rule now needs to be deleted by the caller.

   //---------------------------------------------------------------------------
   // Cannot verify the consistency if the TClass object is not present
   //---------------------------------------------------------------------------
   if( (checkConsistency != kNoCheck) && !fClass )
      return kFALSE;

   if( !rule->IsValid() )
      return kFALSE;

   //---------------------------------------------------------------------------
   // If we don't check the consistency then we should just add the object
   //---------------------------------------------------------------------------
   if( checkConsistency == kNoCheck ) {
      if( rule->GetEmbed() )
         fPersistentRules->Add( rule );
      else
         fRemainingRules->Add( rule );
      fAllRules->Add( rule );
      return kTRUE;
   }

   //---------------------------------------------------------------------------
   // Check if all of the target data members specified in the rule are
   // present int the target class
   //---------------------------------------------------------------------------
   TObject* obj;
   // Check only if we have some information about the class, otherwise we have
   // nothing to check against
   bool streamerInfosTest;
   {
     R__LOCKGUARD2(gInterpreterMutex);
     streamerInfosTest = (fClass->GetStreamerInfos()==0 || fClass->GetStreamerInfos()->GetEntries()==0);
   }
   
   if( rule->GetTarget() && !(fClass->TestBit(TClass::kIsEmulation) && streamerInfosTest) ) {
      TObjArrayIter titer( rule->GetTarget() );
      while( (obj = titer.Next()) ) {
         TObjString* str = (TObjString*)obj;
         if( !fClass->GetDataMember( str->GetString() ) && !fClass->GetBaseClass( str->GetString() ) ) {
            if (checkConsistency == kCheckAll) {
               if (errmsg) {
                  errmsg->Form("the target member (%s) is unknown",str->GetString().Data());
               }
               return kFALSE;
            } else {
               // We ignore the rules that do not apply ...
               delete rule;
               return kTRUE;
            }
         }
      }
   }

   //---------------------------------------------------------------------------
   // Check if there is a rule conflicting with this one
   //---------------------------------------------------------------------------
   if ( GetRules()->GetSize() && !fSourceTargets ) {
      TObjArrayIter it( GetRules() );
      while ( (obj = it.Next()) ) 
         ProcessSourceTargets( (TSchemaRule*)obj );
   }

   TObjArrayIter it1( rule->GetTarget() );
   RuleMap_t::iterator it2;
   RuleKey_t* key = new RuleKey_t( rule->GetSourceClass(), "" );
   std::vector<std::pair<Int_t, Int_t> >::const_iterator it_ver1, it_ver2;
   std::vector<UInt_t>::const_iterator it_chk;

   while ( (obj = it1.Next()) ) {
      key->second = ((TObjString*)obj)->GetString().Data();
      for ( it2 = fSourceTargets->lower_bound(*key); it2 != fSourceTargets->upper_bound(*key); ++it2 ) {
         if ( *(it2->second) == *rule ) {
            // The rules are duplicate from each other,
            // just ignore the new ones.
            if (errmsg) {
               *errmsg = "it conflicts with one of the other rules";
            }
            delete key;
            return kTRUE; 
         }  
     
         // check checksum
         if ( rule->GetChecksum() && it2->second->GetChecksum() ) {
            for ( it_chk = rule->GetChecksum()->begin(); it_chk != rule->GetChecksum()->end(); ++it_chk ) {
               if ( !it2->second->TestChecksum(*it_chk) ) 
                  continue;
               
               if (errmsg) {
                  *errmsg = "The existing rule is:\n   ";
                  it2->second->AsString(*errmsg,"s");
                  *errmsg += "\nand the ignored rule is:\n   ";
                  rule->AsString(*errmsg);
                  *errmsg += ".\n";
               }
               delete key;
               return kFALSE;      
            }
         }

         // ckeck versions
         if ( !rule->GetVersion() || !(it2->second->GetVersion()) )
            continue;

         for ( it_ver1 = it2->second->GetVersion()->begin(); it_ver1 != it2->second->GetVersion()->end(); ++it_ver1 ) {
            for ( it_ver2 = rule->GetVersion()->begin(); it_ver2 != rule->GetVersion()->end(); ++it_ver2 ) {
               if ( ! (((it_ver1->first >= it_ver2->first) && (it_ver1->first  <= it_ver2->second)) || 
                        ((it_ver1->first <= it_ver2->first) && (it_ver1->second <= it_ver2->first))) )  
               {
                  continue;
               }
               
               if (errmsg) {
                  *errmsg = "The existing rule is:\n   ";
                  it2->second->AsString(*errmsg,"s");
                  *errmsg += "\nand the ignored rule is:\n   ";
                  rule->AsString(*errmsg);
                  *errmsg += ".\n";
               }
               delete key;
               return kFALSE;      
            }
         }
      }
   }
   delete key;

   //---------------------------------------------------------------------------
   // No conflicts - insert the rules
   //---------------------------------------------------------------------------
   ProcessSourceTargets( rule );

   if( rule->GetEmbed() )
      fPersistentRules->Add( rule );
   else
      fRemainingRules->Add( rule );
   fAllRules->Add( rule );

   return kTRUE;
}

//------------------------------------------------------------------------------
void TSchemaRuleSet::AsString(TString &out) const
{
   // Fill the string 'out' with the string representation of the rule.

   TObjArrayIter it( fAllRules );
   TSchemaRule *rule;
   while( (rule = (TSchemaRule*)it.Next()) ) {
      rule->AsString(out);
      out += "\n";
   }
}

//------------------------------------------------------------------------------
Bool_t TSchemaRuleSet::HasRuleWithSourceClass( const TString &source ) const
{
   // Return True if we have any rule whose source class is 'source'.

   TObjArrayIter it( fAllRules );
   TObject *obj;
   while( (obj = it.Next()) ) {
      TSchemaRule* rule = (TSchemaRule*)obj;
      if( rule->GetSourceClass() == source )
         return kTRUE;
   }
   // There was no explicit rule, let's see we have implicit rules.
   if (fClass->GetCollectionProxy()) {
      if (fClass->GetCollectionProxy()->GetValueClass() == 0) {
         // We have a numeric collection, let see if the target is
         // also a numeric collection.
         TClass *src = TClass::GetClass(source);
         if (src && src->GetCollectionProxy() &&
             src->GetCollectionProxy()->HasPointers() == fClass->GetCollectionProxy()->HasPointers()) {
            TVirtualCollectionProxy *proxy = src->GetCollectionProxy();
            if (proxy->GetValueClass() == 0) {
               return kTRUE;
            }
         }
      } else {
         TClass *vTargetClass = fClass->GetCollectionProxy()->GetValueClass();
         TClass *src = TClass::GetClass(source);
         if (vTargetClass->GetSchemaRules()) {
            if (src && src->GetCollectionProxy() &&
                src->GetCollectionProxy()->HasPointers() == fClass->GetCollectionProxy()->HasPointers()) {
               TClass *vSourceClass = src->GetCollectionProxy()->GetValueClass();
               if (vSourceClass) {
                  return vTargetClass->GetSchemaRules()->HasRuleWithSourceClass( vSourceClass->GetName() );
               }
            }
         }
      }
   } else if (!strncmp(fClass->GetName(),"std::pair<",10) || !strncmp(fClass->GetName(),"pair<",5)) {
      if (!strncmp(source,"std::pair<",10) || !strncmp(source,"pair<",5)) {
         // std::pair can be converted into each other if both its parameter can be converted into
         // each other.
         TClass *src = TClass::GetClass(source);
         if (!src) {
            Error("HasRuleWithSourceClass","Can not find the TClass for %s when matching with %s\n",source.Data(),fClass->GetName());
            return kFALSE;
         }
         TVirtualStreamerInfo *sourceInfo = src->GetStreamerInfo();
         TVirtualStreamerInfo *targetInfo = fClass->GetStreamerInfo();
         if (!sourceInfo) {
            Error("HasRuleWithSourceClass","Can not find the StreamerInfo for %s when matching with %s\n",source.Data(),fClass->GetName());
            return kFALSE;
         }
         if (!targetInfo) {
            Error("HasRuleWithSourceClass","Can not find the StreamerInfo for target class %s\n",fClass->GetName());
            return kFALSE;
         }
         for(int i = 0 ; i<2 ; ++i) {
            TStreamerElement *sourceElement = (TStreamerElement*)sourceInfo->GetElements()->At(i);
            TStreamerElement *targetElement = (TStreamerElement*)targetInfo->GetElements()->At(i);
            if (sourceElement->GetClass()) {
               if (!targetElement->GetClass()) {
                  return kFALSE;
               }
               if (sourceElement->GetClass() == targetElement->GetClass()) {
                  continue;
               }
               TSchemaRuleSet *rules = sourceElement->GetClass()->GetSchemaRules();
               if (!rules || !rules->HasRuleWithSourceClass( targetElement->GetClass()->GetName() ) ) {
                  return kFALSE;
               }
            } else if (targetElement->GetClass()) {
               return kFALSE;
            } else {
               // both side are numeric element we can deal with it.
            }
         }
         // Both side are pairs and have convertible types, let records this as a renaming rule
         ROOT::TSchemaRule *ruleobj = new ROOT::TSchemaRule();
         ruleobj->SetSourceClass(source);
         ruleobj->SetTargetClass(fClass->GetName());
         ruleobj->SetVersion("[1-]");
         const_cast<TSchemaRuleSet*>(this)->AddRule(ruleobj);
         return kTRUE;
      }
   }
   return kFALSE;
}

//------------------------------------------------------------------------------
const TObjArray* TSchemaRuleSet::FindRules( const TString &source ) const
{
   // Return all the rules that are about the given 'source' class.
   // User has to delete the returned array
   TObject*      obj;
   TObjArrayIter it( fAllRules );
   TObjArray*    arr = new TObjArray();
   arr->SetOwner( kFALSE );

   while( (obj = it.Next()) ) {
      TSchemaRule* rule = (TSchemaRule*)obj;
      if( rule->GetSourceClass() == source )
         arr->Add( rule );
   }

#if 0
   // Le't's see we have implicit rules.
   if (fClass->GetCollectionProxy()) {
      if (fClass->GetCollectionProxy()->GetValueClass() == 0
          && (fClass->GetCollectionProxy()->GetCollectionType() == TClassEdit::kVector
              || (fClass->GetCollectionProxy()->GetProperties() & TVirtualCollectionProxy::kIsEmulated))) {
         // We have a numeric collection, let see if the target is
         // also a numeric collection (humm just a vector for now)
         TClass *src = TClass::GetClass(source);
         if (src && src->GetCollectionProxy()) {
            TVirtualCollectionProxy *proxy = src->GetCollectionProxy();
            if (proxy->GetValueClass() == 0) {
               // ... would need to check if we already have
               // the rule (or any rule?)
            }
         }
      }
   }
#endif
   return arr;
}

//------------------------------------------------------------------------------
const TSchemaMatch* TSchemaRuleSet::FindRules( const TString &source, Int_t version ) const
{
   // Return all the rules that applies to the specified version of the given 'source' class.
   // User has to delete the returned array

   TObject*      obj;
   TObjArrayIter it( fAllRules );
   TSchemaMatch* arr = new TSchemaMatch();
   arr->SetOwner( kFALSE );

   while( (obj = it.Next()) ) {
      TSchemaRule* rule = (TSchemaRule*)obj;
      if( rule->GetSourceClass() == source && rule->TestVersion( version ) )
         arr->Add( rule );
   }

   if( arr->GetEntriesFast() )
      return arr;
   else {
      delete arr;
      return 0;
   }
}

//------------------------------------------------------------------------------
const TSchemaMatch* TSchemaRuleSet::FindRules( const TString &source, UInt_t checksum ) const
{
   // Return all the rules that applies to the specified checksum of the given 'source' class.
   // User has to delete the returned array

   TObject*      obj;
   TObjArrayIter it( fAllRules );
   TSchemaMatch* arr = new TSchemaMatch();
   arr->SetOwner( kFALSE );

   while( (obj = it.Next()) ) {
      TSchemaRule* rule = (TSchemaRule*)obj;
      if( rule->GetSourceClass() == source && rule->TestChecksum( checksum ) )
         arr->Add( rule );
   }

   if( arr->GetEntriesFast() )
      return arr;
   else {
      delete arr;
      return 0;
   }
}

//------------------------------------------------------------------------------
const TSchemaMatch* TSchemaRuleSet::FindRules( const TString &source, Int_t version, UInt_t checksum ) const
{
   // Return all the rules that applies to the specified version OR checksum of the given 'source' class.
   // User has to delete the returned array

   TObject*      obj;
   TObjArrayIter it( fAllRules );
   TSchemaMatch* arr = new TSchemaMatch();
   arr->SetOwner( kFALSE );

   while( (obj = it.Next()) ) {
      TSchemaRule* rule = (TSchemaRule*)obj;
      if( rule->GetSourceClass() == source && ( rule->TestVersion( version ) || rule->TestChecksum( checksum ) ) )
         arr->Add( rule );
   }

   if( arr->GetEntriesFast() )
      return arr;
   else {
      delete arr;
      return 0;
   }
}

//------------------------------------------------------------------------------
TClass* TSchemaRuleSet::GetClass()
{
   return fClass;
}

//------------------------------------------------------------------------------
UInt_t TSchemaRuleSet::GetClassCheckSum() const
{
   if (fCheckSum == 0 && fClass) {
      const_cast<TSchemaRuleSet*>(this)->fCheckSum = fClass->GetCheckSum();
   }
   return fCheckSum;
}

//------------------------------------------------------------------------------
TString TSchemaRuleSet::GetClassName() const
{
   return fClassName;
}

//------------------------------------------------------------------------------
Int_t TSchemaRuleSet::GetClassVersion() const
{
   return fVersion;
}

//------------------------------------------------------------------------------
const TObjArray* TSchemaRuleSet::GetRules() const
{
   return fAllRules;
}

//------------------------------------------------------------------------------
const TObjArray* TSchemaRuleSet::GetPersistentRules() const
{
   return fPersistentRules;
}

//------------------------------------------------------------------------------
void TSchemaRuleSet::RemoveRule( TSchemaRule* rule )
{
   if ( rule->GetTarget() ) {
      TObjArrayIter it1( rule->GetTarget() );
      TObject *obj;
      RuleMap_t::iterator it2;
      RuleKey_t *key = new RuleKey_t( rule->GetSourceClass(), "" );
      
      while( (obj = it1.Next()) ) {
         key->second = ((TObjString*)obj)->GetString().Data();
         for ( it2 = fSourceTargets->lower_bound(*key); it2 != fSourceTargets->upper_bound(*key); ++it2 ) {
            if ( it2->second == rule )
               fSourceTargets->erase( it2 );
         }
      }
      delete key;
   }

   // Remove given rule from the set - the rule is not being deleted!
   fPersistentRules->Remove( rule );
   fRemainingRules->Remove( rule );
   fAllRules->Remove( rule );
}

//------------------------------------------------------------------------------
void TSchemaRuleSet::RemoveRules( TObjArray* rules )
{
   // remove given array of rules from the set - the rules are not being deleted!
   TObject*      obj;
   TObjArrayIter it( rules );

   while( (obj = it.Next()) ) 
      RemoveRule( (TSchemaRule*)obj );
}

//------------------------------------------------------------------------------
void TSchemaRuleSet::SetClass( TClass* cls )
{
   // Set the TClass associated with this rule set.

   fClass     = cls;
   fClassName = cls->GetName();
   fVersion   = cls->GetClassVersion();
}


//------------------------------------------------------------------------------
const TSchemaRule* TSchemaMatch::GetRuleWithSource( const TString& name ) const
{
   // Return the rule that has 'name' as a source.

   for( Int_t i = 0; i < GetEntries(); ++i ) {
      TSchemaRule* rule = (ROOT::TSchemaRule*)At(i);
      if( rule->HasSource( name ) ) return rule;
   }
   return 0;
}

//------------------------------------------------------------------------------
const TSchemaRule* TSchemaMatch::GetRuleWithTarget( const TString& name ) const
{
   // Return the rule that has 'name' as a target.

   for( Int_t i=0; i<GetEntries(); ++i) {
      ROOT::TSchemaRule *rule = (ROOT::TSchemaRule*)At(i);
      if( rule->HasTarget( name ) ) return rule;
   }
   return 0;
}

//------------------------------------------------------------------------------
Bool_t TSchemaMatch::HasRuleWithSource( const TString& name, Bool_t needingAlloc ) const
{
   // Return true if the set of rules has at least one rule that has the data
   // member named 'name' as a source.
   // If needingAlloc is true, only the rule that requires the data member to
   // be cached will be taken in consideration.

   for( Int_t i = 0; i < GetEntries(); ++i ) {
      TSchemaRule* rule = (ROOT::TSchemaRule*)At(i);
      if( rule->HasSource( name ) ) {
         if (needingAlloc) {
            const TObjArray *targets = rule->GetTarget();
            if (targets && (targets->GetEntries() > 1 || targets->GetEntries()==0) ) {
               return kTRUE;
            }
            if (targets && name != targets->UncheckedAt(0)->GetName() ) {
               return kTRUE;
            }
            // If the rule has the same source and target and does not
            // have any actions, then it does not need allocation.
            if (rule->GetReadFunctionPointer() || rule->GetReadRawFunctionPointer()) {
               return kTRUE;
            }
         } else {
            return kTRUE;
         }
      }
   }
   return kFALSE;
}

//------------------------------------------------------------------------------
Bool_t TSchemaMatch::HasRuleWithTarget( const TString& name, Bool_t willset ) const
{
   // Return true if the set of rules has at least one rule that has the data
   // member named 'name' as a target.
   // If willset is true, only the rule that will set the value of the data member.

   for( Int_t i=0; i<GetEntries(); ++i) {
      ROOT::TSchemaRule *rule = (ROOT::TSchemaRule*)At(i);
      if( rule->HasTarget( name ) ) {
         if (willset) {
            const TObjArray *targets = rule->GetTarget();
            if (targets && (targets->GetEntries() > 1 || targets->GetEntries()==0) ) {
               return kTRUE;
            }
            const TObjArray *sources = rule->GetSource();
            if (sources && (sources->GetEntries() > 1 || sources->GetEntries()==0) ) {
               return kTRUE;
            }
            if (sources && name != sources->UncheckedAt(0)->GetName() ) {
               return kTRUE;
            }
            // If the rule has the same source and target and does not
            // have any actions, then it will not directly set the value.
            if (rule->GetReadFunctionPointer() || rule->GetReadRawFunctionPointer()) {
               return kTRUE;
            }
         } else {
            return kTRUE;
         }
      }
   }
   return kFALSE;
}

//______________________________________________________________________________
void TSchemaRuleSet::Streamer(TBuffer &R__b)
{
   // Stream an object of class ROOT::TSchemaRuleSet.

   if (R__b.IsReading()) {
      R__b.ReadClassBuffer(ROOT::TSchemaRuleSet::Class(),this);
      fAllRules->Clear();
      fAllRules->AddAll(fPersistentRules);
   } else {
      GetClassCheckSum();
      R__b.WriteClassBuffer(ROOT::TSchemaRuleSet::Class(),this);
   }
}

void TSchemaRuleSet::ProcessSourceTargets( TSchemaRule* rule )
{
   TObjArrayIter it( rule->GetTarget() );
   TObject* obj;
   RuleKey_t* key = new RuleKey_t( rule->GetSourceClass(), "" );

   while ( (obj = it.Next()) ) { 
      key->second = ((TObjString*)obj)->GetString().Data();
      fSourceTargets->insert( std::pair<RuleKey_t, TSchemaRule*>(*key, rule ) );
   }

   delete key;
}
