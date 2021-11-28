#pragma once

#include "spirv.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace spirv {
struct Id {
  unsigned id{};

  Id() = default;
  explicit Id(unsigned value) : id(value) {}

  explicit operator unsigned() const { return id; }
  explicit operator bool() const { return id != 0; }

  bool operator==(Id other) const { return id == other.id; }
  bool operator!=(Id other) const { return id != other.id; }
  bool operator<(Id other) const { return id < other.id; }
  bool operator>(Id other) const { return id > other.id; }
  bool operator<=(Id other) const { return id <= other.id; }
  bool operator>=(Id other) const { return id >= other.id; }
};

struct Type : Id {};
struct ScalarType : Type {};
struct VoidType : Type {};
struct BoolType : ScalarType {};
struct IntType : ScalarType {};
struct SIntType : IntType {};
struct UIntType : IntType {};
struct FloatType : ScalarType {};
struct VectorType : Type {};
struct MatrixType : Type {};
struct SamplerType : Type {};
struct ImageType : Type {};
struct SampledImageType : Type {};
struct ArrayType : Type {};
struct RuntimeArrayType : Type {};
struct StructType : Type {};
struct PointerType : Type {};
struct FunctionType : Type {};

struct ExtInstSet : Id {};
struct Function : Id {};
struct Block : Id {};
struct Value : Id {};

struct BoolValue : Value {};
struct IntValue : Value {};
struct SIntValue : IntValue {};
struct UIntValue : IntValue {};
struct FloatValue : Value {};
struct StructValue : Value {};
struct PointerValue : Value {};
struct VectorValue : Value {};

template <typename T>
requires(std::is_base_of_v<Value, T>) struct ConstantValue : T {
};

struct AnyConstantValue : Value {
  AnyConstantValue() = default;

  template <typename T> AnyConstantValue(ConstantValue<T> specialization) {
    id = specialization.id;
  }

  template <typename T>
  AnyConstantValue &operator=(ConstantValue<T> specialization) {
    id = specialization.id;
    return *this;
  }

  template <typename T> explicit operator ConstantValue<T>() {
    ConstantValue<T> result;
    result.id = id;
    return result;
  }
};

template <typename T>
requires(std::is_base_of_v<Type, T>) struct VectorOfType : VectorType {
};

template <typename T>
requires(std::is_base_of_v<Type, T>) struct VectorOfValue : VectorValue {
};

template <typename T>
requires(std::is_base_of_v<Type, T>) struct PointerToType : PointerType {
};

template <typename T>
requires(std::is_base_of_v<Type, T>) struct PointerToValue : PointerValue {
};

struct StructPointerValue : Value {};

struct VariableValue : PointerValue {};

namespace detail {
template <typename T> struct TypeToValueImpl;

template <> struct TypeToValueImpl<BoolType> { using type = BoolValue; };
template <> struct TypeToValueImpl<IntType> { using type = IntValue; };
template <> struct TypeToValueImpl<SIntType> { using type = SIntValue; };
template <> struct TypeToValueImpl<UIntType> { using type = UIntValue; };
template <> struct TypeToValueImpl<FloatType> { using type = FloatValue; };
template <> struct TypeToValueImpl<StructType> { using type = StructValue; };
template <> struct TypeToValueImpl<PointerType> { using type = PointerValue; };
template <> struct TypeToValueImpl<VariableValue> { using type = PointerValue; };
template <> struct TypeToValueImpl<VectorType> { using type = VectorValue; };

template <typename T> struct TypeToValueImpl<PointerToType<T>> {
  using type = PointerToValue<T>;
};
template <typename T> struct TypeToValueImpl<VectorOfType<T>> {
  using type = VectorOfValue<T>;
};

} // namespace detail

template <typename T>
using TypeToValue = typename detail::TypeToValueImpl<T>::type;

template <typename T>
requires(std::is_base_of_v<Type, T>) struct ScalarOrVectorOfValue : Value {
  ScalarOrVectorOfValue() = default;

  ScalarOrVectorOfValue(TypeToValue<T> scalar) { id = scalar.id; }
  ScalarOrVectorOfValue(VectorOfValue<T> vector) { id = vector.id; }
};

using ConstantBool = ConstantValue<BoolValue>;
using ConstantSInt = ConstantValue<SIntValue>;
using ConstantUInt = ConstantValue<UIntValue>;
using ConstantInt = ConstantValue<IntValue>;
using ConstantFloat = ConstantValue<FloatValue>;

template <typename ToT, typename FromT>
requires(std::is_base_of_v<FromT, ToT> &&std::is_base_of_v<Id, FromT>) ToT
    cast(FromT from) {
  ToT result;
  result.id = from.id;
  return result;
}

[[gnu::const]] static unsigned calcStringWordCount(std::string_view string) {
  return (string.length() + 1 + (sizeof(std::uint32_t) - 1)) /
         sizeof(std::uint32_t);
}

class RegionPusher {
  std::uint32_t *mPtr = nullptr;
  std::size_t mCount = 0;

public:
  RegionPusher() = default;
  RegionPusher(std::uint32_t *ptr, std::size_t count)
      : mPtr(ptr), mCount(count) {}
  RegionPusher(const RegionPusher &) = delete;
  RegionPusher(RegionPusher &&other) : mPtr(other.mPtr), mCount(other.mCount) {
    other.mPtr = nullptr;
  }

  ~RegionPusher() { assert(mPtr == nullptr || mCount == 0); }

  RegionPusher &operator=(RegionPusher &&other) {
    other.swap(*this);
    return *this;
  }

  void swap(RegionPusher &other) {
    std::swap(mPtr, other.mPtr);
    std::swap(mCount, other.mCount);
  }

  void pushWord(unsigned word) {
    assert(mCount > 0);
    *mPtr++ = word;
    --mCount;
  }

  void pushString(std::string_view string) {
    auto nwords = calcStringWordCount(string);
    assert(mCount >= nwords);

    auto dst = reinterpret_cast<char *>(mPtr);
    std::memcpy(dst, string.data(), string.length());
    std::memset(dst + string.length(), 0,
                nwords * sizeof(std::uint32_t) - string.length());
    mPtr += nwords;
    mCount -= nwords;
  }
};

class Region {
  std::vector<std::uint32_t> mData;

public:
  Region() = default;
  Region(std::size_t expInstCount) { mData.reserve(expInstCount); }

  void clear() { mData.clear(); }

  const std::uint32_t *data() const { return mData.data(); }
  std::size_t size() const { return mData.size(); }

  RegionPusher pushOp(spv::Op op, unsigned wordCount) {
    assert(wordCount >= 1);
    auto offset = mData.size();
    mData.resize(mData.size() + wordCount);
    RegionPusher pusher(mData.data() + offset, wordCount);
    pusher.pushWord((static_cast<unsigned>(op) & spv::OpCodeMask) |
                    (wordCount << spv::WordCountShift));

    return pusher;
  }

  void pushRegion(const Region &other) {
    auto offset = mData.size();
    mData.resize(mData.size() + other.size());
    std::memcpy(mData.data() + offset, other.data(),
                other.size() * sizeof(std::uint32_t));
  }
};

struct IdGenerator {
  std::uint32_t bounds = 1;

  template <typename T>
  requires(std::is_base_of_v<Id, T>) T newId() {
    T result;
    result.id = bounds++;
    return result;
  }

  void reset() { bounds = 1; }
};

class BlockBuilder {
  IdGenerator *mIdGenerator = nullptr;

  template <typename T> auto newId() -> decltype(mIdGenerator->newId<T>()) {
    return mIdGenerator->newId<T>();
  }

public:
  Region phiRegion;
  Region variablesRegion;
  Region bodyRegion;
  Block id;

  BlockBuilder(IdGenerator &idGenerator, Block id,
               std::size_t expInstructionsCount)
      : mIdGenerator(&idGenerator), bodyRegion{expInstructionsCount}, id(id) {}


  Value createExtInst(Type resultType, ExtInstSet set,
                      std::uint32_t instruction,
                      std::span<const Value> operands) {
    auto region = bodyRegion.pushOp(spv::Op::OpExtInst, 5 + operands.size());
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(set));
    region.pushWord(static_cast<unsigned>(instruction));
    for (auto operand : operands) {
      region.pushWord(static_cast<unsigned>(operand));
    }
    return id;
  }


  VariableValue createVariable(Type type, spv::StorageClass storageClass,
                              std::optional<Value> initializer = {}) {
    auto region = variablesRegion.pushOp(spv::Op::OpVariable,
                                    4 + (initializer.has_value() ? 1 : 0));
    auto id = newId<VariableValue>();
    region.pushWord(static_cast<unsigned>(type));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(storageClass));
    if (initializer.has_value()) {
      region.pushWord(static_cast<unsigned>(initializer.value()));
    }
    return id;
  }

  Value createFunctionCall(Type resultType, Function function,
                           std::span<const Value> arguments) {
    auto region =
        bodyRegion.pushOp(spv::Op::OpFunctionCall, 4 + arguments.size());
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(function));
    for (auto argument : arguments) {
      region.pushWord(static_cast<unsigned>(argument));
    }
    return id;
  }

  // composite
  Value createVectorExtractDynamic(Type resultType, Value vector,
                                   IntValue index) {
    auto region = bodyRegion.pushOp(spv::Op::OpVectorExtractDynamic, 5);
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(vector));
    region.pushWord(static_cast<unsigned>(index));
    return id;
  }

  Value createVectorInsertDynamic(Type resultType, Value vector,
                                  Value component, IntValue index) {
    auto region = bodyRegion.pushOp(spv::Op::OpVectorInsertDynamic, 6);
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(vector));
    region.pushWord(static_cast<unsigned>(component));
    region.pushWord(static_cast<unsigned>(index));
    return id;
  }

  Value createVectorShuffle(Type resultType, Value vector1, Value vector2,
                            std::span<const std::uint32_t> components) {
    auto region =
        bodyRegion.pushOp(spv::Op::OpVectorShuffle, 5 + components.size());
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(vector1));
    region.pushWord(static_cast<unsigned>(vector2));

    for (auto component : components) {
      region.pushWord(static_cast<unsigned>(component));
    }
    return id;
  }

  Value createCompositeConstruct(Type resultType,
                                 std::span<const Value> constituents) {
    auto region = bodyRegion.pushOp(spv::Op::OpCompositeConstruct,
                                    3 + constituents.size());
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));

    for (auto constituent : constituents) {
      region.pushWord(static_cast<unsigned>(constituent));
    }
    return id;
  }

  Value createCompositeExtract(Type resultType, Value composite,
                                 std::span<const std::uint32_t> indexes) {
    auto region = bodyRegion.pushOp(spv::Op::OpCompositeExtract,
                                    4 + indexes.size());
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(composite));

    for (auto index : indexes) {
      region.pushWord(static_cast<unsigned>(index));
    }
    return id;
  }

  // arithmetic
  template <typename T>
  requires(std::is_base_of_v<ScalarType, T>) TypeToValue<T> createInst(
      spv::Op op, T resultType, std::span<const TypeToValue<T>> operands) {
    auto region = bodyRegion.pushOp(op, 3 + operands.size());
    auto id = newId<TypeToValue<T>>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    for (auto operand : operands) {
      region.pushWord(static_cast<unsigned>(operand));
    }
    return id;
  }

  Value createInst(spv::Op op, Type resultType,
                              std::span<const Value> operands) {
    auto region = bodyRegion.pushOp(op, 3 + operands.size());
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    for (auto operand : operands) {
      region.pushWord(static_cast<unsigned>(operand));
    }
    return id;
  }

  template <typename T>
  VectorOfValue<T> createInst(spv::Op op, VectorOfType<T> resultType,
                              std::span<const VectorOfValue<T>> operands) {
    auto region = bodyRegion.pushOp(op, 3 + operands.size());
    auto id = newId<VectorOfValue<T>>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    for (auto operand : operands) {
      region.pushWord(static_cast<unsigned>(operand));
    }
    return id;
  }

  template <typename T>
  requires(std::is_same_v<SIntType, T> ||
           std::is_same_v<VectorOfType<SIntType>, T>)
      TypeToValue<T> createSNegate(T resultType, TypeToValue<T> operand) {
    return createInst(spv::Op::OpSNegate, resultType, std::array{operand});
  }

  template <typename T>
  requires(std::is_same_v<FloatType, T> ||
           std::is_same_v<VectorOfType<FloatType>, T>)
      TypeToValue<T> createFNegate(T resultType, TypeToValue<T> operand) {
    return createInst(spv::Op::OpFNegate, resultType, std::array{operand});
  }

  template <typename T>
  requires(std::is_same_v<IntType, T> || std::is_base_of_v<IntType, T> ||
           std::is_same_v<VectorOfType<IntType>, T> ||
           std::is_same_v<VectorOfType<SIntType>, T> ||
           std::is_same_v<VectorOfType<UIntType>, T>)
      TypeToValue<T> createIAdd(T resultType, TypeToValue<T> operand1,
                                TypeToValue<T> operand2) {
    return createInst(spv::Op::OpIAdd, resultType,
                      std::array{operand1, operand2});
  }

  template <typename T>
  requires(std::is_same_v<FloatType, T> ||
           std::is_same_v<VectorOfType<FloatType>, T>)
      TypeToValue<T> createFAdd(T resultType, TypeToValue<T> operand1,
                                TypeToValue<T> operand2) {
    return createInst(spv::Op::OpFAdd, resultType,
                      std::array{operand1, operand2});
  }

  template <typename T>
  requires(std::is_same_v<IntType, T> || std::is_base_of_v<IntType, T> ||
           std::is_same_v<VectorOfType<IntType>, T> ||
           std::is_same_v<VectorOfType<SIntType>, T> ||
           std::is_same_v<VectorOfType<UIntType>, T>) IntValue
      createISub(IntType resultType, IntValue operand1, IntValue operand2) {
    return createInst(spv::Op::OpISub, resultType,
                      std::array{operand1, operand2});
  }

  template <typename T>
  requires(std::is_same_v<FloatType, T> ||
           std::is_same_v<VectorOfType<FloatType>, T>)
      TypeToValue<T> createFSub(T resultType, TypeToValue<T> operand1,
                                TypeToValue<T> operand2) {
    return createInst(spv::Op::OpFSub, resultType,
                      std::array{operand1, operand2});
  }

  template <typename T>
  requires(std::is_same_v<IntType, T> || std::is_base_of_v<IntType, T> ||
           std::is_same_v<VectorOfType<IntType>, T> ||
           std::is_same_v<VectorOfType<SIntType>, T> ||
           std::is_same_v<VectorOfType<UIntType>, T>)
      TypeToValue<T> createIMul(T resultType, TypeToValue<T> operand1,
                                TypeToValue<T> operand2) {
    return createInst(spv::Op::OpIMul, resultType,
                      std::array{operand1, operand2});
  }

  template <typename T>
  requires(std::is_same_v<FloatType, T> ||
           std::is_same_v<VectorOfType<FloatType>, T>)
      TypeToValue<T> createFMul(T resultType, TypeToValue<T> operand1,
                                TypeToValue<T> operand2) {
    return createInst(spv::Op::OpFMul, resultType,
                      std::array{operand1, operand2});
  }

  template <typename T>
  requires(std::is_same_v<UIntType, T> ||
           std::is_same_v<VectorOfType<UIntType>, T>)
      TypeToValue<T> createUDiv(T resultType, TypeToValue<T> operand1,
                                TypeToValue<T> operand2) {
    return createInst(spv::Op::OpUDiv, resultType,
                      std::array{operand1, operand2});
  }
  template <typename T>
  requires(std::is_same_v<SIntType, T> ||
           std::is_same_v<VectorOfType<SIntType>, T>)
      TypeToValue<T> createSDiv(T resultType, TypeToValue<T> operand1,
                                TypeToValue<T> operand2) {
    return createInst(spv::Op::OpSDiv, resultType,
                      std::array{operand1, operand2});
  }

  template <typename T>
  requires(std::is_same_v<FloatType, T> ||
           std::is_same_v<VectorOfType<FloatType>, T>)
      TypeToValue<T> createFDiv(T resultType, TypeToValue<T> operand1,
                                TypeToValue<T> operand2) {
    return createInst(spv::Op::OpFDiv, resultType,
                      std::array{operand1, operand2});
  }
  template <typename T>
  requires(std::is_same_v<UIntType, T> ||
           std::is_same_v<VectorOfType<UIntType>, T>)
      TypeToValue<T> createUMod(T resultType, TypeToValue<T> operand1,
                                TypeToValue<T> operand2) {
    return createInst(spv::Op::OpUMod, resultType,
                      std::array{operand1, operand2});
  }
  template <typename T>
  requires(std::is_same_v<SIntType, T> ||
           std::is_same_v<VectorOfType<SIntType>, T>)
      TypeToValue<T> createSRem(T resultType, TypeToValue<T> operand1,
                                TypeToValue<T> operand2) {
    return createInst(spv::Op::OpSRem, resultType,
                      std::array{operand1, operand2});
  }

  template <typename T>
  requires(std::is_same_v<SIntType, T> ||
           std::is_same_v<VectorOfType<SIntType>, T>)
      TypeToValue<T> createSMod(T resultType, TypeToValue<T> operand1,
                                TypeToValue<T> operand2) {
    return createInst(spv::Op::OpSMod, resultType,
                      std::array{operand1, operand2});
  }
  template <typename T>
  requires(std::is_same_v<FloatType, T> ||
           std::is_same_v<VectorOfType<FloatType>, T>)
      TypeToValue<T> createFRem(T resultType, TypeToValue<T> operand1,
                                TypeToValue<T> operand2) {
    return createInst(spv::Op::OpFRem, resultType,
                      std::array{operand1, operand2});
  }
  template <typename T>
  requires(std::is_same_v<FloatType, T> ||
           std::is_same_v<VectorOfType<FloatType>, T>)
      TypeToValue<T> createFMod(T resultType, TypeToValue<T> operand1,
                                TypeToValue<T> operand2) {
    return createInst(spv::Op::OpFMod, resultType,
                      std::array{operand1, operand2});
  }

  Value createIAddCarry(Type resultType, Value operand1, Value operand2) {
    return createInst(spv::Op::OpIAddCarry, resultType,
                      std::array{operand1, operand2});
  }

  Value createISubBorrow(Type resultType, Value operand1, Value operand2) {
    return createInst(spv::Op::OpISubBorrow, resultType,
                      std::array{operand1, operand2});
  }

  Value createUMulExtended(Type resultType, Value operand1, Value operand2) {
    return createInst(spv::Op::OpUMulExtended, resultType,
                      std::array{operand1, operand2});
  }

  Value createSMulExtended(Type resultType, Value operand1, Value operand2) {
    return createInst(spv::Op::OpSMulExtended, resultType,
                      std::array{operand1, operand2});
  }

  Value createPhi(Type resultType,
                  std::span<const std::pair<Value, Block>> values) {
    auto region = phiRegion.pushOp(spv::Op::OpPhi, 3 + values.size() * 2);
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    for (auto [variable, block] : values) {
      region.pushWord(static_cast<unsigned>(variable));
      region.pushWord(static_cast<unsigned>(block));
    }
    return id;
  }

  void addBlockToPhis(spirv::Block block,
                      std::span<const spirv::Value> values) {
    auto phi = phiRegion.data();
    spirv::Region newPhi(phiRegion.size() * 2);

    assert(block);

    for (std::size_t i = 0, end = phiRegion.size(), index = 0; i < end;
         index++) {
      auto opWordCount = phi[i];

      assert(static_cast<spv::Op>(static_cast<unsigned>(opWordCount) &
                                  spv::OpCodeMask) == spv::Op::OpPhi);
      auto wordCount =
          static_cast<unsigned>(opWordCount) >> spv::WordCountShift;
      auto newOp = newPhi.pushOp(spv::Op::OpPhi, wordCount + 2);

      for (std::size_t j = 1; j < wordCount; ++j) {
        newOp.pushWord(phi[i + j]);
      }

      i += wordCount;

      assert(index < values.size());
      assert(values[index]);

      newOp.pushWord(static_cast<unsigned>(values[index]));
      newOp.pushWord(static_cast<unsigned>(block));
    }

    phiRegion = std::move(newPhi);
  }

  void moveVariablesFrom(BlockBuilder &otherBlock) {
    variablesRegion.pushRegion(otherBlock.variablesRegion);
    otherBlock.variablesRegion.clear();
  }

  template <typename T>
  requires(std::is_base_of_v<Type, T>) TypeToValue<T> createPhi(
      T resultType, std::span<const std::pair<Value, Block>> values) {
    return cast<TypeToValue<T>>(
        createPhi(static_cast<Type>(resultType), values));
  }

  void createLoopMerge(Block mergeBlock, Block continueTarget,
                       spv::LoopControlMask loopControl,
                       std::span<const std::uint32_t> loopControlParameters) {
    auto region = bodyRegion.pushOp(spv::Op::OpLoopMerge,
                                    4 + loopControlParameters.size());
    region.pushWord(static_cast<unsigned>(mergeBlock));
    region.pushWord(static_cast<unsigned>(continueTarget));
    region.pushWord(static_cast<unsigned>(loopControl));

    for (auto loopControlParameter : loopControlParameters) {
      region.pushWord(static_cast<unsigned>(loopControlParameter));
    }
  }

  void createBranch(Block label) {
    auto region = bodyRegion.pushOp(spv::Op::OpBranch, 2);
    region.pushWord(static_cast<unsigned>(label));
  }

  void createBranchConditional(
      BoolValue condition, Block trueLabel, Block falseLabel,
      std::optional<std::pair<std::uint32_t, std::uint32_t>> weights = {}) {
    auto region = bodyRegion.pushOp(spv::Op::OpBranchConditional,
                                    4 + (weights.has_value() ? 1 : 0));
    region.pushWord(static_cast<unsigned>(condition));
    region.pushWord(static_cast<unsigned>(trueLabel));
    region.pushWord(static_cast<unsigned>(falseLabel));

    if (weights.has_value()) {
      region.pushWord(static_cast<unsigned>(weights->first));
      region.pushWord(static_cast<unsigned>(weights->second));
    }
  }

  void createKill() { bodyRegion.pushOp(spv::Op::OpKill, 1); }

  void createReturn() { bodyRegion.pushOp(spv::Op::OpReturn, 1); }

  void createReturnValue(Value value) {
    auto region = bodyRegion.pushOp(spv::Op::OpReturnValue, 2);
    region.pushWord(static_cast<unsigned>(value));
  }

  void createUnreachable() { bodyRegion.pushOp(spv::Op::OpUnreachable, 1); }

  Value createLoad(Type resultType, PointerValue pointer) {
    auto region = bodyRegion.pushOp(spv::Op::OpLoad, 4);
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(pointer));
    return id;
  }

  Value createLoad(Type resultType, PointerValue pointer,
                   spv::MemoryAccessMask memoryAccess,
                   std::span<const std::uint32_t> memoryAccessOperands) {
    auto region =
        bodyRegion.pushOp(spv::Op::OpLoad, 5 + memoryAccessOperands.size());
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(pointer));
    region.pushWord(static_cast<unsigned>(memoryAccess));

    for (auto memoryAccessOperand : memoryAccessOperands) {
      region.pushWord(static_cast<unsigned>(memoryAccessOperand));
    }

    return id;
  }

  template <typename T>
  requires(std::is_base_of_v<Type, T>)
      TypeToValue<T> createLoad(T resultType, PointerValue pointer) {
    return cast<TypeToValue<T>>(
        createLoad(static_cast<Type>(resultType), pointer));
  }

  void createStore(PointerValue pointer, Value object) {
    auto region = bodyRegion.pushOp(spv::Op::OpStore, 3);
    region.pushWord(static_cast<unsigned>(pointer));
    region.pushWord(static_cast<unsigned>(object));
  }

  void createStore(PointerValue pointer, Value object,
                   spv::MemoryAccessMask memoryAccess,
                   std::span<const std::uint32_t> memoryAccessOperands) {
    auto region =
        bodyRegion.pushOp(spv::Op::OpStore, 4 + memoryAccessOperands.size());
    region.pushWord(static_cast<unsigned>(pointer));
    region.pushWord(static_cast<unsigned>(object));
    region.pushWord(static_cast<unsigned>(memoryAccess));

    for (auto memoryAccessOperand : memoryAccessOperands) {
      region.pushWord(static_cast<unsigned>(memoryAccessOperand));
    }
  }

  void createCopyMemory(PointerValue targetPointer,
                        PointerValue sourcePointer) {
    auto region = bodyRegion.pushOp(spv::Op::OpCopyMemory, 3);
    region.pushWord(static_cast<unsigned>(targetPointer));
    region.pushWord(static_cast<unsigned>(sourcePointer));
  }

  void createCopyMemory(PointerValue targetPointer, PointerValue sourcePointer,
                        spv::MemoryAccessMask memoryAccess,
                        std::span<const std::uint32_t> memoryAccessOperands) {
    auto region = bodyRegion.pushOp(spv::Op::OpCopyMemory,
                                    4 + memoryAccessOperands.size());
    region.pushWord(static_cast<unsigned>(targetPointer));
    region.pushWord(static_cast<unsigned>(sourcePointer));
    region.pushWord(static_cast<unsigned>(memoryAccess));
    for (auto memoryAccessOperand : memoryAccessOperands) {
      region.pushWord(static_cast<unsigned>(memoryAccessOperand));
    }
  }

  void
  createCopyMemory(PointerValue targetPointer, PointerValue sourcePointer,
                   spv::MemoryAccessMask targetMemoryAccess,
                   std::span<const std::uint32_t> targetMemoryAccessOperands,
                   spv::MemoryAccessMask sourceMemoryAccess,
                   std::span<const std::uint32_t> sourceMemoryAccessOperands) {
    auto region = bodyRegion.pushOp(spv::Op::OpCopyMemory,
                                    5 + targetMemoryAccessOperands.size() +
                                        sourceMemoryAccessOperands.size());
    region.pushWord(static_cast<unsigned>(targetPointer));
    region.pushWord(static_cast<unsigned>(sourcePointer));
    region.pushWord(static_cast<unsigned>(targetMemoryAccess));
    for (auto memoryAccessOperand : targetMemoryAccessOperands) {
      region.pushWord(static_cast<unsigned>(memoryAccessOperand));
    }
    region.pushWord(static_cast<unsigned>(sourceMemoryAccess));
    for (auto memoryAccessOperand : sourceMemoryAccessOperands) {
      region.pushWord(static_cast<unsigned>(memoryAccessOperand));
    }
  }

  UIntValue createArrayLength(UIntType resultType,
                              PointerToValue<StructType> structure,
                              std::uint32_t member) {
    auto region = bodyRegion.pushOp(spv::Op::OpArrayLength, 5);
    auto id = newId<UIntValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(structure));
    region.pushWord(static_cast<unsigned>(member));
    return id;
  }

  BoolValue createPtrEqual(BoolType resultType, PointerValue operand1,
                           PointerValue operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpPtrEqual, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }

  BoolValue createPtrNotEqual(BoolType resultType, PointerValue operand1,
                              PointerValue operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpPtrNotEqual, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }

  PointerValue createAccessChain(PointerType resultType, PointerValue base,
                                 std::span<const SIntValue> indices) {
    auto region = bodyRegion.pushOp(spv::Op::OpAccessChain, 4 + indices.size());
    auto id = newId<PointerValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(base));

    for (auto index : indices) {
      region.pushWord(static_cast<unsigned>(index));
    }
    return id;
  }

  PointerValue createInBoundsAccessChain(PointerType resultType,
                                         PointerValue base,
                                         std::span<const SIntValue> indices) {
    auto region =
        bodyRegion.pushOp(spv::Op::OpInBoundsAccessChain, 4 + indices.size());
    auto id = newId<PointerValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(base));

    for (auto index : indices) {
      region.pushWord(static_cast<unsigned>(index));
    }
    return id;
  }

  // conversion
  Value createConvertFToU(Type resultType, Value operand) {
    auto region = bodyRegion.pushOp(spv::Op::OpConvertFToU, 4);
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand));
    return id;
  }
  Value createConvertFToS(Type resultType, Value operand) {
    auto region = bodyRegion.pushOp(spv::Op::OpConvertFToS, 4);
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand));
    return id;
  }
  Value createConvertSToF(Type resultType, Value operand) {
    auto region = bodyRegion.pushOp(spv::Op::OpConvertSToF, 4);
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand));
    return id;
  }
  Value createConvertUToF(Type resultType, Value operand) {
    auto region = bodyRegion.pushOp(spv::Op::OpConvertUToF, 4);
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand));
    return id;
  }
  Value createUConvert(Type resultType, Value operand) {
    auto region = bodyRegion.pushOp(spv::Op::OpUConvert, 4);
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand));
    return id;
  }
  Value createSConvert(Type resultType, Value operand) {
    auto region = bodyRegion.pushOp(spv::Op::OpSConvert, 4);
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand));
    return id;
  }
  Value createFConvert(Type resultType, Value operand) {
    auto region = bodyRegion.pushOp(spv::Op::OpFConvert, 4);
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand));
    return id;
  }
  Value createBitcast(Type resultType, Value operand) {
    auto region = bodyRegion.pushOp(spv::Op::OpBitcast, 4);
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand));
    return id;
  }

  // bit
  Value createShiftRightLogical(Type resultType, Value base, Value shift) {
    auto region = bodyRegion.pushOp(spv::Op::OpShiftRightLogical, 5);
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(base));
    region.pushWord(static_cast<unsigned>(shift));
    return id;
  }

  Value createShiftRightArithmetic(Type resultType, Value base, Value shift) {
    auto region = bodyRegion.pushOp(spv::Op::OpShiftRightArithmetic, 5);
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(base));
    region.pushWord(static_cast<unsigned>(shift));
    return id;
  }

  Value createShiftLeftLogical(Type resultType, Value base, Value shift) {
    auto region = bodyRegion.pushOp(spv::Op::OpShiftLeftLogical, 5);
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(base));
    region.pushWord(static_cast<unsigned>(shift));
    return id;
  }

  Value createBitwiseOr(Type resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpBitwiseOr, 5);
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }

  Value createBitwiseXor(Type resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpBitwiseXor, 5);
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }

  Value createBitwiseAnd(Type resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpBitwiseAnd, 5);
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }

  Value createNot(Type resultType, Value operand) {
    auto region = bodyRegion.pushOp(spv::Op::OpNot, 4);
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand));
    return id;
  }

  // logic
  BoolValue createLogicalEqual(BoolType resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpLogicalEqual, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }
  BoolValue createLogicalNotEqual(BoolType resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpLogicalNotEqual, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }

  Value createSelect(Type resultType, Value condition, Value object1, Value object2) {
    auto region = bodyRegion.pushOp(spv::Op::OpSelect, 6);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(condition));
    region.pushWord(static_cast<unsigned>(object1));
    region.pushWord(static_cast<unsigned>(object2));
    return id;
  }

  BoolValue createIEqual(BoolType resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpIEqual, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }
  BoolValue createINotEqual(BoolType resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpINotEqual, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }
  BoolValue createUGreaterThan(BoolType resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpUGreaterThan, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }
  BoolValue createSGreaterThan(BoolType resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpSGreaterThan, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }
  BoolValue createUGreaterThanEqual(BoolType resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpUGreaterThanEqual, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }
  BoolValue createSGreaterThanEqual(BoolType resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpSGreaterThanEqual, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }
  BoolValue createULessThan(BoolType resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpULessThan, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }
  BoolValue createSLessThan(BoolType resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpSLessThan, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }
  BoolValue createULessThanEqual(BoolType resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpULessThanEqual, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }
  BoolValue createSLessThanEqual(BoolType resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpSLessThanEqual, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }

  BoolValue createFOrdEqual(BoolType resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpFOrdEqual, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }
  BoolValue createFUnordEqual(BoolType resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpFUnordEqual, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }
  BoolValue createFOrdNotEqual(BoolType resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpFOrdNotEqual, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }
  BoolValue createFUnordNotEqual(BoolType resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpFUnordNotEqual, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }
  BoolValue createFOrdLessThan(BoolType resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpFOrdLessThan, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }
  BoolValue createFUnordLessThan(BoolType resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpFUnordLessThan, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }
  BoolValue createFOrdLessThanEqual(BoolType resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpFOrdLessThanEqual, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }
  BoolValue createFUnordLessThanEqual(BoolType resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpFUnordLessThanEqual, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }
  BoolValue createFOrdGreaterThan(BoolType resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpFOrdGreaterThan, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }
  BoolValue createFUnordGreaterThan(BoolType resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpFUnordGreaterThan, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }
  BoolValue createFOrdGreaterThanEqual(BoolType resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpFOrdGreaterThanEqual, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }
  BoolValue createFUnordGreaterThanEqual(BoolType resultType, Value operand1, Value operand2) {
    auto region = bodyRegion.pushOp(spv::Op::OpFUnordGreaterThanEqual, 5);
    auto id = newId<BoolValue>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(operand1));
    region.pushWord(static_cast<unsigned>(operand2));
    return id;
  }
};

class FunctionBuilder {
  IdGenerator *mIdGenerator = nullptr;

  template <typename T> auto newId() -> decltype(mIdGenerator->newId<T>()) {
    return mIdGenerator->newId<T>();
  }

public:
  Region paramsRegion;
  Region bodyRegion;
  Function id;

  FunctionBuilder(IdGenerator &idGenerator, Function id,
                  std::size_t expInstructionsCount)
      : mIdGenerator(&idGenerator), bodyRegion{expInstructionsCount}, id(id) {}

  Value createFunctionParameter(Type resultType) {
    auto region = paramsRegion.pushOp(spv::Op::OpFunctionParameter, 3);
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    return id;
  }

  BlockBuilder createBlockBuilder(std::size_t expInstructionsCount) {
    auto id = newId<Block>();

    return BlockBuilder(*mIdGenerator, id, expInstructionsCount);
  }

  void insertBlock(const BlockBuilder &builder) {
    auto region = bodyRegion.pushOp(spv::Op::OpLabel, 2);
    region.pushWord(static_cast<unsigned>(builder.id));

    bodyRegion.pushRegion(builder.variablesRegion);
    bodyRegion.pushRegion(builder.phiRegion);
    bodyRegion.pushRegion(builder.bodyRegion);
  }
};

class SpirvBuilder {
  IdGenerator *mIdGenerator = nullptr;

  Region capabilityRegion;
  Region extensionRegion;
  Region extInstRegion;
  Region memoryModelRegion;
  Region entryPointRegion;
  Region executionModeRegion;
  Region debugRegion;
  Region annotationRegion;
  Region globalRegion;
  Region functionDeclRegion;
  Region functionRegion;

  template <typename T> auto newId() -> decltype(mIdGenerator->newId<T>()) {
    return mIdGenerator->newId<T>();
  }

public:
  SpirvBuilder() = default;
  SpirvBuilder(const SpirvBuilder &) = delete;
  SpirvBuilder(SpirvBuilder &&) = default;
  SpirvBuilder &operator=(SpirvBuilder &&) = default;

  SpirvBuilder(IdGenerator &idGenerator, std::size_t expInstructionsCount)
      : mIdGenerator(&idGenerator), capabilityRegion{1}, extensionRegion{1},
        extInstRegion{4}, memoryModelRegion{3}, entryPointRegion{1},
        executionModeRegion{1}, debugRegion{0}, annotationRegion{1},
        globalRegion{1}, functionDeclRegion{1}, functionRegion{
                                                    expInstructionsCount} {}

  SpirvBuilder clone() const {
    SpirvBuilder result;
    result.mIdGenerator = mIdGenerator;
    result.capabilityRegion = capabilityRegion;
    result.extensionRegion = extensionRegion;
    result.extInstRegion = extInstRegion;
    result.memoryModelRegion = memoryModelRegion;
    result.entryPointRegion = entryPointRegion;
    result.executionModeRegion = executionModeRegion;
    result.debugRegion = debugRegion;
    result.annotationRegion = annotationRegion;
    result.globalRegion = globalRegion;
    result.functionDeclRegion = functionDeclRegion;
    result.functionRegion = functionRegion;
    return result;
  }

  void swap(SpirvBuilder &other) {
    std::swap(mIdGenerator, other.mIdGenerator);
    std::swap(capabilityRegion, other.capabilityRegion);
    std::swap(extensionRegion, other.extensionRegion);
    std::swap(extInstRegion, other.extInstRegion);
    std::swap(memoryModelRegion, other.memoryModelRegion);
    std::swap(entryPointRegion, other.entryPointRegion);
    std::swap(executionModeRegion, other.executionModeRegion);
    std::swap(debugRegion, other.debugRegion);
    std::swap(annotationRegion, other.annotationRegion);
    std::swap(globalRegion, other.globalRegion);
    std::swap(functionDeclRegion, other.functionDeclRegion);
    std::swap(functionRegion, other.functionRegion);
  }

  void reset() {
    mIdGenerator->reset();
    capabilityRegion.clear();
    extensionRegion.clear();
    extInstRegion.clear();
    memoryModelRegion.clear();
    entryPointRegion.clear();
    executionModeRegion.clear();
    debugRegion.clear();
    annotationRegion.clear();
    globalRegion.clear();
    functionDeclRegion.clear();
    functionRegion.clear();
  }

  std::vector<std::uint32_t> build(std::uint32_t spirvVersion,
                                   std::uint32_t generatorMagic) {
    const std::size_t headerSize = 5;
    std::size_t finalSize = headerSize;

    std::array regions = {
        &capabilityRegion,   &extensionRegion,  &extInstRegion,
        &memoryModelRegion,  &entryPointRegion, &executionModeRegion,
        &debugRegion,        &annotationRegion, &globalRegion,
        &functionDeclRegion, &functionRegion,
    };

    for (auto region : regions) {
      finalSize += region->size();
    }

    std::vector<std::uint32_t> result;
    result.resize(finalSize);

    result[0] = spv::MagicNumber;
    result[1] = spirvVersion;
    result[2] = generatorMagic;
    result[3] = mIdGenerator->bounds;
    result[4] = 0; // instruction schema

    std::size_t currentOffset = headerSize;

    for (auto region : regions) {
      std::memcpy(result.data() + currentOffset, region->data(),
                  region->size() * sizeof(std::uint32_t));
      currentOffset += region->size();
    }

    return result;
  }

  // misc
  Value createUndef(Type resultType) {
    auto region = globalRegion.pushOp(spv::Op::OpUndef, 3);
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    return id;
  }

  template <typename T>
  requires(std::is_base_of_v<Type, T>)
      TypeToValue<T> createUndef(T resultType) {
    return cast<TypeToValue<T>>(createUndef(resultType));
  }

  // annotation
  void createDecorate(Id target, spv::Decoration decoration,
                      std::span<const std::uint32_t> decorationOperands) {
    auto region = annotationRegion.pushOp(spv::Op::OpDecorate,
                                          3 + decorationOperands.size());
    region.pushWord(static_cast<unsigned>(target));
    region.pushWord(static_cast<unsigned>(decoration));

    for (auto decorationOperand : decorationOperands) {
      region.pushWord(static_cast<unsigned>(decorationOperand));
    }
  }

  void createMemberDecorate(StructType structureType, std::uint32_t member,
                            spv::Decoration decoration,
                            std::span<const std::uint32_t> decorationOperands) {
    auto region = annotationRegion.pushOp(spv::Op::OpMemberDecorate,
                                          4 + decorationOperands.size());
    region.pushWord(static_cast<unsigned>(structureType));
    region.pushWord(static_cast<unsigned>(member));
    region.pushWord(static_cast<unsigned>(decoration));

    for (auto decorationOperand : decorationOperands) {
      region.pushWord(static_cast<unsigned>(decorationOperand));
    }
  }

  void createDecorateId(Id target, spv::Decoration decoration,
                        std::span<const Value> decorationOperands) {
    auto region = annotationRegion.pushOp(spv::Op::OpDecorateId,
                                          3 + decorationOperands.size());
    region.pushWord(static_cast<unsigned>(target));
    region.pushWord(static_cast<unsigned>(decoration));

    for (auto decorationOperand : decorationOperands) {
      region.pushWord(static_cast<unsigned>(decorationOperand));
    }
  }

  void createDecorateString(
      Id target, spv::Decoration decoration,
      std::string_view firstDecorationOperand,
      std::span<const std::string_view> decorationOperands = {}) {
    std::size_t decorationOperandsLen =
        calcStringWordCount(firstDecorationOperand);

    for (auto decorationOperand : decorationOperands) {
      decorationOperandsLen += calcStringWordCount(decorationOperand);
    }

    auto region = annotationRegion.pushOp(spv::Op::OpDecorateString,
                                          3 + decorationOperandsLen);
    region.pushWord(static_cast<unsigned>(target));
    region.pushWord(static_cast<unsigned>(decoration));
    region.pushString(firstDecorationOperand);

    for (auto decorationOperand : decorationOperands) {
      region.pushString(decorationOperand);
    }
  }

  void createMemberDecorateString(
      StructType structType, std::uint32_t member, spv::Decoration decoration,
      std::string_view firstDecorationOperand,
      std::span<const std::string_view> decorationOperands = {}) {
    std::size_t decorationOperandsLen =
        calcStringWordCount(firstDecorationOperand);

    for (auto decorationOperand : decorationOperands) {
      decorationOperandsLen += calcStringWordCount(decorationOperand);
    }

    auto region = annotationRegion.pushOp(spv::Op::OpMemberDecorateString,
                                          4 + decorationOperandsLen);
    region.pushWord(static_cast<unsigned>(structType));
    region.pushWord(static_cast<unsigned>(member));
    region.pushWord(static_cast<unsigned>(decoration));
    region.pushString(firstDecorationOperand);

    for (auto decorationOperand : decorationOperands) {
      region.pushString(decorationOperand);
    }
  }

  // extension
  void createExtension(std::string_view name) {
    auto region = extensionRegion.pushOp(spv::Op::OpExtension,
                                         2 + calcStringWordCount(name));
    region.pushString(name);
  }

  ExtInstSet createExtInstImport(std::string_view name) {
    auto region = extInstRegion.pushOp(spv::Op::OpExtInstImport,
                                       2 + calcStringWordCount(name));
    auto id = newId<ExtInstSet>();
    region.pushWord(static_cast<unsigned>(id));
    region.pushString(name);
    return id;
  }

  // mode set
  void createCapability(spv::Capability cap) {
    auto region = capabilityRegion.pushOp(spv::Op::OpCapability, 2);
    region.pushWord(static_cast<unsigned>(cap));
  }

  void setMemoryModel(spv::AddressingModel addressingModel,
                      spv::MemoryModel memoryModel) {
    memoryModelRegion.clear();
    auto region = memoryModelRegion.pushOp(spv::Op::OpMemoryModel, 3);
    region.pushWord(static_cast<std::uint32_t>(addressingModel));
    region.pushWord(static_cast<std::uint32_t>(memoryModel));
  }

  void createEntryPoint(spv::ExecutionModel executionModel, Function entryPoint,
                        std::string_view name,
                        std::span<const VariableValue> interfaces) {
    auto region = entryPointRegion.pushOp(spv::Op::OpEntryPoint,
                                          3 + calcStringWordCount(name) +
                                              interfaces.size());
    region.pushWord(static_cast<unsigned>(executionModel));
    region.pushWord(static_cast<unsigned>(entryPoint));
    region.pushString(name);
    for (auto iface : interfaces) {
      region.pushWord(static_cast<unsigned>(iface));
    }
  }
  void createExecutionMode(Function entryPoint, spv::ExecutionMode mode,
                           std::span<const std::uint32_t> args) {
    auto region =
        executionModeRegion.pushOp(spv::Op::OpExecutionMode, 3 + args.size());
    region.pushWord(static_cast<unsigned>(entryPoint));
    region.pushWord(static_cast<unsigned>(mode));
    for (auto arg : args) {
      region.pushWord(static_cast<unsigned>(arg));
    }
  }

  void createExecutionModeId(Function entryPoint, spv::ExecutionMode mode,
                             std::span<const Value> args) {
    auto region =
        executionModeRegion.pushOp(spv::Op::OpExecutionModeId, 3 + args.size());
    region.pushWord(static_cast<unsigned>(entryPoint));
    region.pushWord(static_cast<unsigned>(mode));
    for (auto arg : args) {
      region.pushWord(static_cast<unsigned>(arg));
    }
  }

  // type
  VoidType createTypeVoid() {
    auto region = globalRegion.pushOp(spv::Op::OpTypeVoid, 2);
    auto id = newId<VoidType>();
    region.pushWord(static_cast<unsigned>(id));
    return id;
  }
  BoolType createTypeBool() {
    auto region = globalRegion.pushOp(spv::Op::OpTypeBool, 2);
    auto id = newId<BoolType>();
    region.pushWord(static_cast<unsigned>(id));
    return id;
  }
  IntType createTypeInt(std::uint32_t width, bool signedness) {
    auto region = globalRegion.pushOp(spv::Op::OpTypeInt, 4);
    auto id = newId<IntType>();
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(width));
    region.pushWord(static_cast<unsigned>(signedness));
    return id;
  }
  SIntType createTypeSInt(std::uint32_t width) {
    return cast<SIntType>(createTypeInt(width, true));
  }
  UIntType createTypeUInt(std::uint32_t width) {
    return cast<UIntType>(createTypeInt(width, false));
  }
  FloatType createTypeFloat(std::uint32_t width) {
    auto region = globalRegion.pushOp(spv::Op::OpTypeFloat, 3);
    auto id = newId<FloatType>();
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(width));
    return id;
  }
  template <typename T>
  VectorOfType<T> createTypeVector(T componentType,
                                   std::uint32_t componentCount) {
    auto region = globalRegion.pushOp(spv::Op::OpTypeVector, 4);
    auto id = newId<VectorOfType<T>>();
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(componentType));
    region.pushWord(static_cast<unsigned>(componentCount));
    return id;
  }
  MatrixType createTypeMatrix(VectorType columnType,
                              std::uint32_t coulumnCount) {
    auto region = globalRegion.pushOp(spv::Op::OpTypeMatrix, 4);
    auto id = newId<MatrixType>();
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(columnType));
    region.pushWord(static_cast<unsigned>(coulumnCount));
    return id;
  }

  ImageType createTypeImage(spv::Dim dim, std::uint32_t depth,
                            std::uint32_t arrayed, std::uint32_t ms,
                            std::uint32_t sampled, spv::ImageFormat imageFormat,
                            std::optional<spv::AccessQualifier> access = {}) {
    auto region = globalRegion.pushOp(spv::Op::OpTypeImage,
                                      9 + (access.has_value() ? 1 : 0));
    auto id = newId<ImageType>();
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(dim));
    region.pushWord(static_cast<unsigned>(depth));
    region.pushWord(static_cast<unsigned>(arrayed));
    region.pushWord(static_cast<unsigned>(ms));
    region.pushWord(static_cast<unsigned>(sampled));
    region.pushWord(static_cast<unsigned>(imageFormat));

    if (access.has_value()) {
      region.pushWord(static_cast<unsigned>(*access));
    }

    return id;
  }

  SamplerType createTypeSampler() {
    auto region = globalRegion.pushOp(spv::Op::OpTypeSampler, 2);
    auto id = newId<SamplerType>();
    region.pushWord(static_cast<unsigned>(id));
    return id;
  }

  SampledImageType createTypeSampledImage(ImageType imageType) {
    auto region = globalRegion.pushOp(spv::Op::OpTypeSampledImage, 3);
    auto id = newId<SampledImageType>();
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(imageType));
    return id;
  }

  ArrayType createTypeArray(Type elementType, AnyConstantValue count) {
    auto region = globalRegion.pushOp(spv::Op::OpTypeArray, 4);
    auto id = newId<ArrayType>();
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(elementType));
    region.pushWord(static_cast<unsigned>(count));
    return id;
  }

  RuntimeArrayType createTypeRuntimeArray(Type elementType) {
    auto region = globalRegion.pushOp(spv::Op::OpTypeRuntimeArray, 3);
    auto id = newId<RuntimeArrayType>();
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(elementType));

    return id;
  }

  StructType createTypeStruct(std::span<const Type> members) {
    auto region =
        globalRegion.pushOp(spv::Op::OpTypeStruct, 2 + members.size());
    auto id = newId<StructType>();
    region.pushWord(static_cast<unsigned>(id));

    for (auto member : members) {
      region.pushWord(static_cast<unsigned>(member));
    }

    return id;
  }

  PointerType createTypePointer(spv::StorageClass storageClass, Type type) {
    auto region = globalRegion.pushOp(spv::Op::OpTypePointer, 4);
    auto id = newId<PointerType>();
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(storageClass));
    region.pushWord(static_cast<unsigned>(type));
    return id;
  }

  template <typename T>
  requires(std::is_base_of_v<Type, T>) PointerToType<T> createTypePointer(
      spv::StorageClass storageClass, T type) {
    return cast<PointerToType<T>>(
        createTypePointer(storageClass, static_cast<Type>(type)));
  }

  FunctionType createTypeFunction(Type returnType,
                                  std::span<const Type> parameters) {
    auto region =
        globalRegion.pushOp(spv::Op::OpTypeFunction, 3 + parameters.size());
    auto id = newId<FunctionType>();
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(returnType));

    for (auto param : parameters) {
      region.pushWord(static_cast<unsigned>(param));
    }

    return id;
  }

  // constant
  ConstantBool createConstantTrue(BoolType type) {
    auto region = globalRegion.pushOp(spv::Op::OpConstantTrue, 3);
    auto id = newId<ConstantBool>();
    region.pushWord(static_cast<unsigned>(type));
    region.pushWord(static_cast<unsigned>(id));
    return id;
  }

  ConstantBool createConstantFalse(BoolType type) {
    auto region = globalRegion.pushOp(spv::Op::OpConstantFalse, 3);
    auto id = newId<ConstantBool>();
    region.pushWord(static_cast<unsigned>(type));
    region.pushWord(static_cast<unsigned>(id));
    return id;
  }

  template <typename T>
  requires(std::is_base_of_v<Type, T>)
      ConstantValue<TypeToValue<T>> createConstant(
          T type, std::span<const std::uint32_t> values) {
    auto region = globalRegion.pushOp(spv::Op::OpConstant, 3 + values.size());
    auto id = newId<ConstantValue<TypeToValue<T>>>();
    region.pushWord(static_cast<unsigned>(type));
    region.pushWord(static_cast<unsigned>(id));
    for (auto value : values) {
      region.pushWord(static_cast<unsigned>(value));
    }
    return id;
  }

  template <typename T>
  requires(std::is_base_of_v<Type, T>)
      ConstantValue<TypeToValue<T>> createConstant32(T type,
                                                     std::uint32_t value) {
    return createConstant(type, std::array{value});
  }

  template <typename T>
  requires(std::is_base_of_v<Type, T>)
      ConstantValue<TypeToValue<T>> createConstant64(T type,
                                                     std::uint64_t value) {
    return createConstant(type,
                          std::array{static_cast<std::uint32_t>(value),
                                     static_cast<std::uint32_t>(value >> 32)});
  }

  // memory
  VariableValue createVariable(Type type, spv::StorageClass storageClass,
                              std::optional<Value> initializer = {}) {
    auto region = globalRegion.pushOp(spv::Op::OpVariable,
                                      4 + (initializer.has_value() ? 1 : 0));
    auto id = newId<VariableValue>();
    region.pushWord(static_cast<unsigned>(type));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(storageClass));
    if (initializer.has_value()) {
      region.pushWord(static_cast<unsigned>(initializer.value()));
    }
    return id;
  }

private:
  void createFunction(Function id, Type resultType,
                      spv::FunctionControlMask functionControl,
                      Type functionType) {
    auto region = functionRegion.pushOp(spv::Op::OpFunction, 5);
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    region.pushWord(static_cast<unsigned>(functionControl));
    region.pushWord(static_cast<unsigned>(functionType));
  }

  Value createFunctionParameter(Type resultType) {
    auto region = functionRegion.pushOp(spv::Op::OpFunctionParameter, 3);
    auto id = newId<Value>();
    region.pushWord(static_cast<unsigned>(resultType));
    region.pushWord(static_cast<unsigned>(id));
    return id;
  }

  void createFunctionEnd() { functionRegion.pushOp(spv::Op::OpFunctionEnd, 1); }

public:
  FunctionBuilder createFunctionBuilder(std::size_t expInstructionsCount) {
    auto id = newId<Function>();
    return FunctionBuilder(*mIdGenerator, id, expInstructionsCount);
  }

  void insertFunctionDeclaration(const FunctionBuilder &function,
                                 Type resultType,
                                 spv::FunctionControlMask functionControl,
                                 Type functionType) {
    createFunction(function.id, resultType, functionControl, functionType);
    functionRegion.pushRegion(function.paramsRegion);
    createFunctionEnd();
  }

  void insertFunction(const FunctionBuilder &function, Type resultType,
                      spv::FunctionControlMask functionControl,
                      Type functionType) {
    createFunction(function.id, resultType, functionControl, functionType);
    functionRegion.pushRegion(function.paramsRegion);
    functionRegion.pushRegion(function.bodyRegion);
    createFunctionEnd();
  }

  BlockBuilder createBlockBuilder(std::size_t expInstructionsCount) {
    auto id = newId<Block>();

    return BlockBuilder(*mIdGenerator, id, expInstructionsCount);
  }
};
} // namespace spirv
