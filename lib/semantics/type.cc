// Copyright (c) 2018, NVIDIA CORPORATION.  All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "type.h"
#include "expression.h"
#include "scope.h"
#include "semantics.h"
#include "symbol.h"
#include "../evaluate/fold.h"
#include "../evaluate/tools.h"
#include "../evaluate/type.h"
#include "../parser/characters.h"

namespace Fortran::semantics {

LazyExpr::LazyExpr(SomeExpr &&expr) : u_{CopyableExprPtr{std::move(expr)}} {}

MaybeExpr LazyExpr::Get() { return static_cast<const LazyExpr *>(this)->Get(); }

const MaybeExpr LazyExpr::Get() const {
  if (auto *ptr{std::get_if<CopyableExprPtr>(&u_)}) {
    return **ptr;
  } else {
    return std::nullopt;
  }
}

bool LazyExpr::Resolve(SemanticsContext &context) {
  if (auto *expr{std::get_if<const parser::Expr *>(&u_)}) {
    if (!*expr) {
      u_ = ErrorInExpr{};
    } else if (MaybeExpr maybeExpr{AnalyzeExpr(context, **expr)}) {
      u_ = CopyableExprPtr{
          evaluate::Fold(context.foldingContext(), std::move(*maybeExpr))};
    } else {
      u_ = ErrorInExpr{};
    }
  }
  return std::holds_alternative<CopyableExprPtr>(u_);
}

std::ostream &operator<<(std::ostream &o, const LazyExpr &x) {
  std::visit(
      common::visitors{
          [&](const parser::Expr *x) { o << (x ? "UNRESOLVED" : "EMPTY"); },
          [&](const LazyExpr::ErrorInExpr &) { o << "ERROR"; },
          [&](const LazyExpr::CopyableExprPtr &x) { x->AsFortran(o); },
      },
      x.u_);
  return o;
}

void DerivedTypeSpec::set_scope(const Scope &scope) {
  CHECK(!scope_);
  CHECK(scope.kind() == Scope::Kind::DerivedType);
  scope_ = &scope;
}

std::ostream &operator<<(std::ostream &o, const DerivedTypeSpec &x) {
  o << "TYPE(" << x.name().ToString();
  if (!x.paramValues_.empty()) {
    bool first = true;
    o << '(';
    for (auto &pair : x.paramValues_) {
      if (first) {
        first = false;
      } else {
        o << ',';
      }
      if (auto &name{pair.first}) {
        o << name->ToString() << '=';
      }
      o << pair.second;
    }
    o << ')';
  }
  return o << ')';
}

Bound::Bound(int bound)
  : category_{Category::Explicit},
    expr_{SomeExpr{evaluate::AsExpr(
        evaluate::Constant<evaluate::SubscriptInteger>{bound})}} {}

void Bound::Resolve(SemanticsContext &context) {
  if (isExplicit()) {
    expr_.Resolve(context);
  }
}

std::ostream &operator<<(std::ostream &o, const Bound &x) {
  if (x.isAssumed()) {
    o << '*';
  } else if (x.isDeferred()) {
    o << ':';
  } else {
    o << x.expr_;
  }
  return o;
}

std::ostream &operator<<(std::ostream &o, const ShapeSpec &x) {
  if (x.lb_.isAssumed()) {
    CHECK(x.ub_.isAssumed());
    o << "..";
  } else {
    if (!x.lb_.isDeferred()) {
      o << x.lb_;
    }
    o << ':';
    if (!x.ub_.isDeferred()) {
      o << x.ub_;
    }
  }
  return o;
}

ParamValue::ParamValue(const parser::Expr &expr)
  : category_{Category::Explicit}, expr_{expr} {}

void ParamValue::ResolveExplicit(SemanticsContext &context) {
  CHECK(isExplicit());
  expr_.Resolve(context);
}

std::ostream &operator<<(std::ostream &o, const ParamValue &x) {
  if (x.isAssumed()) {
    o << '*';
  } else if (x.isDeferred()) {
    o << ':';
  } else {
    o << x.GetExplicit();
  }
  return o;
}

IntrinsicTypeSpec::IntrinsicTypeSpec(TypeCategory category, int kind)
  : category_{category}, kind_{kind} {
  CHECK(category != TypeCategory::Derived);
  CHECK(kind > 0);
}

std::ostream &operator<<(std::ostream &os, const IntrinsicTypeSpec &x) {
  os << parser::ToUpperCaseLetters(common::EnumToString(x.category()));
  if (x.kind() != 0) {
    os << '(' << x.kind() << ')';
  }
  return os;
}

DeclTypeSpec::DeclTypeSpec(const IntrinsicTypeSpec &intrinsic)
  : category_{Intrinsic}, typeSpec_{intrinsic} {}
DeclTypeSpec::DeclTypeSpec(Category category, DerivedTypeSpec &derived)
  : category_{category}, typeSpec_{&derived} {
  CHECK(category == TypeDerived || category == ClassDerived);
}
DeclTypeSpec::DeclTypeSpec(Category category) : category_{category} {
  CHECK(category == TypeStar || category == ClassStar);
}
const IntrinsicTypeSpec &DeclTypeSpec::intrinsicTypeSpec() const {
  CHECK(category_ == Intrinsic);
  return typeSpec_.intrinsic;
}
DerivedTypeSpec &DeclTypeSpec::derivedTypeSpec() {
  CHECK(category_ == TypeDerived || category_ == ClassDerived);
  return *typeSpec_.derived;
}
const DerivedTypeSpec &DeclTypeSpec::derivedTypeSpec() const {
  CHECK(category_ == TypeDerived || category_ == ClassDerived);
  return *typeSpec_.derived;
}
bool DeclTypeSpec::operator==(const DeclTypeSpec &that) const {
  if (category_ != that.category_) {
    return false;
  }
  switch (category_) {
  case Intrinsic: return typeSpec_.intrinsic == that.typeSpec_.intrinsic;
  case TypeDerived:
  case ClassDerived: return typeSpec_.derived == that.typeSpec_.derived;
  default: return true;
  }
}

std::ostream &operator<<(std::ostream &o, const DeclTypeSpec &x) {
  switch (x.category()) {
  case DeclTypeSpec::Intrinsic: return o << x.intrinsicTypeSpec();
  case DeclTypeSpec::TypeDerived: return o << x.derivedTypeSpec();
  case DeclTypeSpec::ClassDerived:
    return o << "CLASS(" << x.derivedTypeSpec().name().ToString() << ')';
  case DeclTypeSpec::TypeStar: return o << "TYPE(*)";
  case DeclTypeSpec::ClassStar: return o << "CLASS(*)";
  default: CRASH_NO_CASE; return o;
  }
}

void ProcInterface::set_symbol(const Symbol &symbol) {
  CHECK(!type_);
  symbol_ = &symbol;
}
void ProcInterface::set_type(const DeclTypeSpec &type) {
  CHECK(!symbol_);
  type_ = type;
}

std::ostream &operator<<(std::ostream &o, const GenericSpec &x) {
  switch (x.kind()) {
  case GenericSpec::GENERIC_NAME: return o << x.genericName().ToString();
  case GenericSpec::OP_DEFINED:
    return o << '(' << x.definedOp().ToString() << ')';
  case GenericSpec::ASSIGNMENT: return o << "ASSIGNMENT(=)";
  case GenericSpec::READ_FORMATTED: return o << "READ(FORMATTED)";
  case GenericSpec::READ_UNFORMATTED: return o << "READ(UNFORMATTED)";
  case GenericSpec::WRITE_FORMATTED: return o << "WRITE(FORMATTED)";
  case GenericSpec::WRITE_UNFORMATTED: return o << "WRITE(UNFORMATTED)";
  case GenericSpec::OP_ADD: return o << "OPERATOR(+)";
  case GenericSpec::OP_CONCAT: return o << "OPERATOR(//)";
  case GenericSpec::OP_DIVIDE: return o << "OPERATOR(/)";
  case GenericSpec::OP_MULTIPLY: return o << "OPERATOR(*)";
  case GenericSpec::OP_POWER: return o << "OPERATOR(**)";
  case GenericSpec::OP_SUBTRACT: return o << "OPERATOR(-)";
  case GenericSpec::OP_AND: return o << "OPERATOR(.AND.)";
  case GenericSpec::OP_EQ: return o << "OPERATOR(.EQ.)";
  case GenericSpec::OP_EQV: return o << "OPERATOR(.EQV.)";
  case GenericSpec::OP_GE: return o << "OPERATOR(.GE.)";
  case GenericSpec::OP_GT: return o << "OPERATOR(.GT.)";
  case GenericSpec::OP_LE: return o << "OPERATOR(.LE.)";
  case GenericSpec::OP_LT: return o << "OPERATOR(.LT.)";
  case GenericSpec::OP_NE: return o << "OPERATOR(.NE.)";
  case GenericSpec::OP_NEQV: return o << "OPERATOR(.NEQV.)";
  case GenericSpec::OP_NOT: return o << "OPERATOR(.NOT.)";
  case GenericSpec::OP_OR: return o << "OPERATOR(.OR.)";
  case GenericSpec::OP_XOR: return o << "OPERATOR(.XOR.)";
  default: CRASH_NO_CASE;
  }
}

class ExprResolver {
public:
  ExprResolver(SemanticsContext &context) : context_{context} {}
  void Resolve() { Resolve(context_.globalScope()); }

private:
  SemanticsContext &context_;

  void Resolve(Scope &);
  void Resolve(Symbol &);
  void Resolve(Bound &bound) { bound.Resolve(context_); }
  void Resolve(LazyExpr &expr) { expr.Resolve(context_); }
};

void ExprResolver::Resolve(Scope &scope) {
  for (auto &pair : scope) {
    Resolve(*pair.second);
  }
  for (auto &child : scope.children()) {
    Resolve(child);
  }
}
void ExprResolver::Resolve(Symbol &symbol) {
  if (auto *type{symbol.GetType()}) {
    if (type->category() == DeclTypeSpec::TypeDerived) {
      DerivedTypeSpec &dts{type->derivedTypeSpec()};
      for (auto &nameAndValue : dts.paramValues()) {
        // &[name, value] elicits "unused variable" warnings
        auto &value{nameAndValue.second};
        if (value.isExplicit()) {
          value.ResolveExplicit(context_);
        }
      }
    }
  }
  if (auto *details{symbol.detailsIf<ObjectEntityDetails>()}) {
    Resolve(details->init());
    for (ShapeSpec &shapeSpec : details->shape()) {
      Resolve(shapeSpec.lb_);
      Resolve(shapeSpec.ub_);
    }
  } else if (auto *details{symbol.detailsIf<TypeParamDetails>()}) {
    Resolve(details->init());
  }
}

void ResolveSymbolExprs(SemanticsContext &context) {
  ExprResolver(context).Resolve();
}
}
