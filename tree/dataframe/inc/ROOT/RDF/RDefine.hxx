// Author: Enrico Guiraud, Danilo Piparo CERN  09/2018

/*************************************************************************
 * Copyright (C) 1995-2018, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_RDF_RDEFINE
#define ROOT_RDF_RDEFINE

#include "ROOT/RDF/ColumnReaderUtils.hxx"
#include "ROOT/RDF/RColumnReaderBase.hxx"
#include "ROOT/RDF/RDefineBase.hxx"
#include "ROOT/RDF/RLoopManager.hxx"
#include "ROOT/RDF/Utils.hxx"
#include "ROOT/RStringView.hxx"
#include "ROOT/TypeTraits.hxx"
#include "RtypesCore.h"

#include <array>
#include <deque>
#include <type_traits>
#include <utility> // std::index_sequence
#include <vector>

class TTreeReader;

namespace ROOT {
namespace Detail {
namespace RDF {

using namespace ROOT::TypeTraits;

// clang-format off
namespace CustomColExtraArgs {
struct None{};
struct Slot{};
struct SlotAndEntry{};
}
// clang-format on

template <typename F, typename ExtraArgsTag = CustomColExtraArgs::None>
class R__CLING_PTRCHECK(off) RDefine final : public RDefineBase {
   // shortcuts
   using NoneTag = CustomColExtraArgs::None;
   using SlotTag = CustomColExtraArgs::Slot;
   using SlotAndEntryTag = CustomColExtraArgs::SlotAndEntry;
   // other types
   using FunParamTypes_t = typename CallableTraits<F>::arg_types;
   using ColumnTypesTmp_t =
      RDFInternal::RemoveFirstParameterIf_t<std::is_same<ExtraArgsTag, SlotTag>::value, FunParamTypes_t>;
   using ColumnTypes_t =
      RDFInternal::RemoveFirstTwoParametersIf_t<std::is_same<ExtraArgsTag, SlotAndEntryTag>::value, ColumnTypesTmp_t>;
   using TypeInd_t = std::make_index_sequence<ColumnTypes_t::list_size>;
   using ret_type = typename CallableTraits<F>::ret_type;
   // Avoid instantiating vector<bool> as `operator[]` returns temporaries in that case. Use std::deque instead.
   using ValuesPerSlot_t =
      std::conditional_t<std::is_same<ret_type, bool>::value, std::deque<ret_type>, std::vector<ret_type>>;

   F fExpression;
   ValuesPerSlot_t fLastResults;

   /// Column readers per slot and per input column
   std::vector<std::array<std::unique_ptr<RColumnReaderBase>, ColumnTypes_t::list_size>> fValues;

   template <typename... ColTypes, std::size_t... S>
   void UpdateHelper(unsigned int slot, Long64_t entry, TypeList<ColTypes...>, std::index_sequence<S...>, NoneTag)
   {
      fLastResults[slot * RDFInternal::CacheLineStep<ret_type>()] =
         fExpression(fValues[slot][S]->template Get<ColTypes>(entry)...);
      // silence "unused parameter" warnings in gcc
      (void)slot;
      (void)entry;
   }

   template <typename... ColTypes, std::size_t... S>
   void UpdateHelper(unsigned int slot, Long64_t entry, TypeList<ColTypes...>, std::index_sequence<S...>, SlotTag)
   {
      fLastResults[slot * RDFInternal::CacheLineStep<ret_type>()] =
         fExpression(slot, fValues[slot][S]->template Get<ColTypes>(entry)...);
      // silence "unused parameter" warnings in gcc
      (void)slot;
      (void)entry;
   }

   template <typename... ColTypes, std::size_t... S>
   void
   UpdateHelper(unsigned int slot, Long64_t entry, TypeList<ColTypes...>, std::index_sequence<S...>, SlotAndEntryTag)
   {
      fLastResults[slot * RDFInternal::CacheLineStep<ret_type>()] =
         fExpression(slot, entry, fValues[slot][S]->template Get<ColTypes>(entry)...);
      // silence "unused parameter" warnings in gcc
      (void)slot;
      (void)entry;
   }

public:
   RDefine(std::string_view name, std::string_view type, F expression, const ROOT::RDF::ColumnNames_t &columns,
           const RDFInternal::RColumnRegister &colRegister, RLoopManager &lm)
      : RDefineBase(name, type, colRegister, lm, columns), fExpression(std::move(expression)),
        fLastResults(lm.GetNSlots() * RDFInternal::CacheLineStep<ret_type>()), fValues(lm.GetNSlots())
   {
   }

   RDefine(const RDefine &) = delete;
   RDefine &operator=(const RDefine &) = delete;

   void InitSlot(TTreeReader *r, unsigned int slot) final
   {
      RDFInternal::RColumnReadersInfo info{fColumnNames, fColRegister, fIsDefine.data(), fLoopManager->GetDSValuePtrs(),
                                           fLoopManager->GetDataSource()};
      fValues[slot] = RDFInternal::MakeColumnReaders(slot, r, ColumnTypes_t{}, info);
      fLastCheckedEntry[slot * RDFInternal::CacheLineStep<Long64_t>()] = -1;
   }

   /// Return the (type-erased) address of the Define'd value for the given processing slot.
   void *GetValuePtr(unsigned int slot) final
   {
      return static_cast<void *>(&fLastResults[slot * RDFInternal::CacheLineStep<ret_type>()]);
   }

   /// Update the value at the address returned by GetValuePtr with the content corresponding to the given entry
   void Update(unsigned int slot, Long64_t entry) final
   {
      if (entry != fLastCheckedEntry[slot * RDFInternal::CacheLineStep<Long64_t>()]) {
         // evaluate this define expression, cache the result
         UpdateHelper(slot, entry, ColumnTypes_t{}, TypeInd_t{}, ExtraArgsTag{});
         fLastCheckedEntry[slot * RDFInternal::CacheLineStep<Long64_t>()] = entry;
      }
   }

   void Update(unsigned int /*slot*/, const ROOT::RDF::RSampleInfo &/*id*/) final {}

   const std::type_info &GetTypeId() const { return typeid(ret_type); }

   /// Clean-up operations to be performed at the end of a task.
   void FinaliseSlot(unsigned int slot) final
   {
      for (auto &v : fValues[slot])
         v.reset();
   }
};

} // ns RDF
} // ns Detail
} // ns ROOT

#endif // ROOT_RDF_RDEFINE
