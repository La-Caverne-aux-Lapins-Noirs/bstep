#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cmini
{

struct Position
{
    int line = 1;
    int column = 1;
};

struct Diagnostic : std::runtime_error
{
    Position where;
    Diagnostic(Position where, const std::string &message);
};

enum class Base { integer, character };

struct Type
{
    Base base = Base::integer;
    int pointers = 0;

    bool operator==(const Type &) const = default;
    int bytes() const;
    std::string name() const;
    Type pointerTo() const;
};

struct Address
{
    std::uint64_t object = 0;
    int index = 0;

    bool operator==(const Address &) const = default;
};

struct Value
{
    Type type;
    int number = 0;
    Address address;

    static Value integer(int number);
    static Value pointer(Type type, Address address);
};

struct Object
{
    std::uint64_t baseAddress;
    std::string name;
    Type element;
    bool array;
    bool literal;
    int scope;
    std::vector<std::optional<Value>> cells;

    std::string function;
    std::uint64_t activation = 0;

    std::string label(int index) const;
};

struct Access
{
    Address address;
    bool write;
};

struct Expression;
using Expr = std::shared_ptr<const Expression>;

struct Declaration
{
    Position source;
    Type type;
    std::string name;
    bool array = false;
    std::optional<int> length;
    Expr initializer;
    std::vector<Expr> list;
    bool hasList = false;
};

struct Instruction
{
    enum class Op { declare, evaluate, test, invoke, returnValue, jump, enter, leave, question };
    Op op;
    Position source;
    Expr expression;
    Declaration declaration;
    std::size_t target = 0;
    std::string label;

    Instruction(Op op, Position source, Expr expression = {});
};

struct Parameter
{
    Type type;
    std::string name;
};

struct Function
{
    std::string name;
    std::optional<Type> result;
    std::vector<Parameter> parameters;
    Position source;
    std::size_t entry;
};

struct Question
{
    Position source;
    std::string text;
    std::vector<Expr> expressions;
};

struct Program
{
    std::vector<Instruction> code;
    std::vector<Function> functions;
    std::vector<Question> questions;
    std::size_t entry = 0;

    const Function *function(std::string_view name) const;
    // Syntactic uses of table[index] for live index markers.
    std::vector<std::pair<std::string, std::string>> indexUses;
};

Program compile(std::string_view text, bool withQuestions = false);
Expr compileProbe(std::string_view text);

struct Invocation
{
    std::string function;
    std::uint64_t activation;
    std::size_t resume;
    int callerScope;
};

struct Probe
{
    std::string value;
    bool expired;
    bool truth;
    std::optional<int> number;
};

struct Frame
{
    std::size_t pc = 0;
    std::size_t steps = 0;
    std::uint64_t nextAddress = 0x1000;
    int scope = 0;
    int lastLine = 0;
    bool finished = false;
    std::optional<bool> testResult;
    std::optional<int> returnCode;
    std::optional<Diagnostic> error;
    std::uint64_t nextActivation = 1;
    std::size_t inputOffset = 0;
    std::size_t inputEnd = 0;
    bool inputEof = false;
    std::vector<Invocation> calls;
    std::optional<Value> returned;
    std::vector<Object> objects;
    std::vector<Object> retired;
    std::vector<Access> accesses;
    std::string event;
    std::string output;
    std::string errorOutput;
    std::string terminal;

    std::string_view functionName() const;
    std::uint64_t activation() const;
    const Object *qualified(std::string_view name, bool allowRetired, bool &expired) const;

    const Object *find(std::uint64_t address) const;
    const Object *find(std::string_view name) const;
    std::string describe(Address address) const;
    std::string hex(Address address) const;
};

class Machine
{
public:
    explicit Machine(Program program, bool keepHistory = true);
    void reset();
    bool next();
    bool previous();
    bool submit(std::string_view input);
    void closeInput();
    bool waiting() const { return waiting_; }
    bool inputClosed() const { return frame().inputEof; }
    std::string_view input() const { return std::string_view(input_).substr(0, frame().inputEnd); }
    std::optional<Probe> inspect(const Expr &expression, bool afterScope = true) const;
    const Question *pendingQuestion() const;
    bool canPrevious() const { return cursor_ > 0; }
    const Frame &frame() const { return history_[cursor_]; }
    const Program &program() const { return program_; }
    Position nextPosition() const;
    int nextLine() const { return nextPosition().line; }

private:
    Program program_;
    std::deque<Frame> history_;
    std::size_t historyLimit_;
    std::size_t cursor_ = 0;
    std::string input_;
    bool waiting_ = false;
};

std::string charDisplay(int number);
std::string valueDisplay(const Frame &frame, const Value &value);

} // namespace cmini
