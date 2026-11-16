#include "cmini.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <iomanip>
#include <limits>
#include <sstream>

namespace cmini
{

Diagnostic::Diagnostic(Position position, const std::string &message)
    : std::runtime_error(message), where(position)
{
}

int Type::bytes() const
{
    if (pointers)
        return 8;
    return base == Base::character ? 1 : 4;
}

std::string Type::name() const
{
    return std::string(base == Base::character ? "char" : "int")
        + (pointers ? " " + std::string(pointers, '*') : "");
}

Type Type::pointerTo() const
{
    return {base, pointers + 1};
}

Value Value::integer(int number)
{
    return {{}, number, {}};
}

Value Value::pointer(Type type, Address address)
{
    return {type, 0, address};
}

std::string Object::label(int index) const
{
    return array ? name + "[" + std::to_string(index) + "]" : name;
}

Instruction::Instruction(Op operation, Position position, Expr expr)
    : op(operation), source(position), expression(std::move(expr))
{
}

enum class TokenKind { end, identifier, number, character, string, symbol, question };

struct Token
{
    TokenKind kind;
    std::string text;
    Position source;
    int number = 0;
};

struct Expression
{
    enum class Kind { number, string, name, unary, post, binary, index, call, result };
    Kind kind;
    Token token;
    Expr left;
    Expr right;
    std::vector<Expr> args;
};

namespace
{

constexpr int maxArray = 128;
constexpr std::size_t maxCells = 1024;
constexpr std::size_t maxHistory = 512;
constexpr std::size_t maxOutput = 4096;
constexpr std::size_t maxInput = 8192;
constexpr std::size_t maxRetired = 96;

struct NeedsInput {};

bool builtin(std::string_view name)
{
    return name == "printf" || name == "puts" || name == "putchar"
        || name == "fprintf" || name == "fputs" || name == "fputc" || name == "putc"
        || name == "scanf" || name == "getchar";
}

bool digit(char c)
{
    return c >= '0' && c <= '9';
}

bool nameStart(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool namePart(char c)
{
    return nameStart(c) || digit(c);
}

int hexDigit(char c)
{
    if (digit(c))
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

class Lexer
{
public:
    explicit Lexer(std::string_view input, bool withQuestions = false)
        : input_(input), withQuestions_(withQuestions)
    {
    }

    std::vector<Token> run()
    {
        std::vector<Token> result;
        while (offset_ < input_.size())
        {
            auto question = skip();
            if (!question && offset_ == input_.size())
                break;
            if (result.size() == 6000)
                throw Diagnostic(pos_, "Ce programme dépasse 6 000 éléments de syntaxe.");
            result.push_back(question ? std::move(*question) : token());
        }
        result.push_back({TokenKind::end, "", pos_});
        return result;
    }

private:
    std::string_view input_;
    std::size_t offset_ = 0;
    Position pos_;
    bool withQuestions_;

    char peek(std::size_t ahead = 0) const
    {
        return offset_ + ahead < input_.size() ? input_[offset_ + ahead] : '\0';
    }

    char take()
    {
        char c = peek();
        ++offset_;
        if (c == '\n')
        {
            ++pos_.line;
            pos_.column = 1;
        }
        else if ((static_cast<unsigned char>(c) & 0xc0) != 0x80)
            ++pos_.column;
        return c;
    }

    std::optional<Token> skip()
    {
        while (offset_ < input_.size())
        {
            char c = peek();
            char after = peek(1);
            if (std::isspace(static_cast<unsigned char>(c)))
                take();
            else if (c == '/' && after == '/')
            {
                const auto position = pos_;
                const auto start = offset_ + 2;
                take();
                take();
                while (peek() && peek() != '\n')
                    take();
                if (withQuestions_)
                {
                    auto comment = input_.substr(start, offset_ - start);
                    const auto first = comment.find_first_not_of(" \t");
                    if (first != comment.npos && comment[first] == '?')
                    {
                        comment.remove_prefix(first + 1);
                        const auto body = comment.find_first_not_of(" \t\r");
                        if (body == comment.npos)
                            throw Diagnostic(position, "Indique les valeurs à demander après le point question.");
                        comment.remove_prefix(body);
                        return Token{TokenKind::question, std::string(comment.substr(0,
                            comment.find_last_not_of(" \t\r") + 1)), position};
                    }
                }
            }
            else if (c == '/' && after == '*')
            {
                const Position start = pos_;
                take();
                take();
                while (offset_ < input_.size() && !(peek() == '*' && peek(1) == '/'))
                    take();
                if (offset_ == input_.size())
                    throw Diagnostic(start, "Commentaire /* sans fermeture */.");
                take();
                take();
            }
            else if (c == '#')
            {
                const Position start = pos_;
                std::string line;
                while (peek() && peek() != '\n')
                {
                    char next = take();
                    if (!std::isspace(static_cast<unsigned char>(next)))
                        line += next;
                }
                if (line != "#include<stdio.h>" && line != "#include<stddef.h>")
                    throw Diagnostic(start, "Directives prises en charge : #include <stdio.h> et <stddef.h>.");
            }
            else
                break;
        }
        return std::nullopt;
    }

    char escape(Position start)
    {
        if (!peek() || peek() == '\n')
            throw Diagnostic(start, "Séquence d'échappement incomplète.");
        switch (const char c = take(); c)
        {
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        case '\\': return '\\';
        case '\'': return '\'';
        case '"': return '"';
        case 'x':
        {
            int value = 0;
            if (hexDigit(peek()) < 0)
                throw Diagnostic(start, "Chiffre hexadécimal attendu après \\x.");
            while (hexDigit(peek()) >= 0)
            {
                value = value * 16 + hexDigit(take());
                if (value > 255)
                    throw Diagnostic(start, "Cette séquence dépasse un octet.");
            }
            return static_cast<char>(value);
        }
        default:
            if (c >= '0' && c <= '7')
            {
                int value = c - '0';
                for (int i = 0; i < 2 && peek() >= '0' && peek() <= '7'; ++i)
                    value = value * 8 + take() - '0';
                if (value > 255)
                    throw Diagnostic(start, "Cette séquence dépasse un octet.");
                return static_cast<char>(value);
            }
            throw Diagnostic(start, "Séquence d'échappement inconnue.");
        }
    }

    Token token()
    {
        const Position start = pos_;
        const auto first = offset_;
        const char c = take();
        if (nameStart(c))
        {
            while (namePart(peek()))
                take();
            return {TokenKind::identifier, std::string(input_.substr(first, offset_ - first)), start};
        }
        if (digit(c))
        {
            while (namePart(peek()))
                take();
            auto word = input_.substr(first, offset_ - first);
            auto digits = word;
            int base = 10;
            if (digits.size() > 1 && digits[0] == '0')
            {
                base = 8;
                if (digits[1] == 'x' || digits[1] == 'X')
                {
                    base = 16;
                    digits.remove_prefix(2);
                }
            }
            int value = 0;
            const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), value, base);
            if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size())
                throw Diagnostic(start, "Entier littéral invalide ou hors plage de int.");
            return {TokenKind::number, std::string(word), start, value};
        }
        if (c == '"' || c == '\'')
        {
            std::string raw;
            while (peek() != c)
            {
                if (!peek() || peek() == '\n')
                    throw Diagnostic(start, "Littéral sans guillemet de fermeture.");
                const char next = take();
                raw += next == '\\' ? escape(start) : next;
            }
            take();
            if (c == '\'')
            {
                if (raw.size() != 1)
                    throw Diagnostic(start, "Un littéral char doit tenir dans un octet.");
                const int byte = static_cast<unsigned char>(raw[0]);
                return {TokenKind::character, raw, start, byte < 128 ? byte : byte - 256};
            }
            return {TokenKind::string, raw, start};
        }
        const std::string two{c, peek()};
        static constexpr std::string_view pairs[] = {
            "==", "!=", "<=", ">=", "&&", "||", "++", "--", "+=", "-=", "*=", "/=", "%="
        };
        if (std::find(std::begin(pairs), std::end(pairs), two) != std::end(pairs))
        {
            take();
            return {TokenKind::symbol, two, start};
        }
        if (c && std::string_view("(){}[];,.+-*/%=<>!&").find(c) != std::string_view::npos)
            return {TokenKind::symbol, std::string(1, c), start};
        throw Diagnostic(start, "Caractère ou opérateur non pris en charge.");
    }
};

Expr expression(Expression::Kind kind, Token token, Expr left = {}, Expr right = {},
                std::vector<Expr> args = {})
{
    return std::make_shared<Expression>(
        Expression{kind, std::move(token), std::move(left), std::move(right), std::move(args)});
}

int precedence(std::string_view op)
{
    if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=")
        return 1;
    if (op == "||") return 2;
    if (op == "&&") return 3;
    if (op == "==" || op == "!=") return 4;
    if (op == "<" || op == "<=" || op == ">" || op == ">=") return 5;
    if (op == "+" || op == "-") return 6;
    if (op == "*" || op == "/" || op == "%") return 7;
    return 0;
}

class Parser
{
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    Program run()
    {
        if (looksLikeFunction())
        {
            while (here().kind != TokenKind::end)
                functionDefinition();
            const auto *main = program_.function("main");
            if (!main || main->result != Type{} || !main->parameters.empty())
                fail("Une fonction int main(void) est attendue.");
            program_.entry = main->entry;
        }
        else
            while (here().kind != TokenKind::end)
                statementOrQuestion(0);
        return std::move(program_);
    }

    Expr probe()
    {
        inProbe_ = true;
        auto value = parseExpr();
        if (here().kind != TokenKind::end)
            fail("Fin d'expression attendue.");
        return value;
    }

    std::vector<Expr> questionExpressions()
    {
        inProbe_ = true;
        std::vector<Expr> expressions;
        do { expressions.push_back(parseExpr()); } while (eat(","));
        if (here().kind != TokenKind::end)
            fail("Sépare les expressions par des virgules.");
        return expressions;
    }

private:
    using Op = Instruction::Op;
    using Kind = Expression::Kind;
    std::vector<Token> tokens_;
    std::size_t cursor_ = 0;
    Program program_;
    int depth_ = 0;
    unsigned expressions_ = 0;
    bool inProbe_ = false;

    const Token &here() const { return tokens_[cursor_]; }
    bool at(std::string_view text) const
    {
        return (here().kind == TokenKind::identifier || here().kind == TokenKind::symbol)
            && here().text == text;
    }

    [[noreturn]] void fail(const std::string &message) const
    {
        throw Diagnostic(here().source, message);
    }

    Token pop()
    {
        if (here().kind == TokenKind::end)
            fail("Fin de programme inattendue.");
        return tokens_[cursor_++];
    }

    bool eat(std::string_view text)
    {
        if (!at(text))
            return false;
        ++cursor_;
        return true;
    }

    Token need(const std::string &text)
    {
        if (!at(text))
            fail("Attendu : " + text);
        return pop();
    }

    bool looksLikeFunction() const
    {
        if (!at("int") && !at("char") && !at("void"))
            return false;
        auto at = cursor_ + 1;
        while (tokens_[at].text == "*")
            ++at;
        return tokens_[at].kind == TokenKind::identifier && tokens_[at + 1].text == "(";
    }

    Type type()
    {
        if (!at("int") && !at("char"))
            fail("Type int ou char attendu.");
        Type parsed{pop().text == "char" ? Base::character : Base::integer, 0};
        while (eat("*"))
        {
            if (++parsed.pointers > 8)
                fail("Trop de niveaux de pointeurs.");
        }
        return parsed;
    }

    Token identifier()
    {
        if (here().kind != TokenKind::identifier)
            fail("Nom attendu.");
        return pop();
    }

    void functionDefinition()
    {
        const Position start = here().source;
        std::optional<Type> result;
        if (!eat("void"))
            result = type();
        auto name = identifier();
        need("(");
        std::vector<Parameter> parameters;
        if (!eat("void") && !at(")"))
        {
            do
            {
                auto parameterType = type();
                auto parameter = identifier();
                if (eat("["))
                {
                    if (here().kind == TokenKind::number)
                        pop();
                    need("]");
                    parameterType = parameterType.pointerTo();
                }
                if (std::ranges::any_of(parameters, [&](const auto &other) { return other.name == parameter.text; }))
                    throw Diagnostic(parameter.source, "Paramètre déclaré deux fois.");
                parameters.push_back({parameterType, std::move(parameter.text)});
            } while (eat(","));
        }
        need(")");
        // Prototypes are accepted so definitions can be ordered freely.
        if (eat(";"))
            return;
        if (builtin(name.text) || program_.function(name.text))
            throw Diagnostic(name.source, "Fonction déjà déclarée.");
        program_.functions.push_back({std::move(name.text), result, std::move(parameters), start, program_.code.size()});
        need("{");
        while (!at("}"))
            statementOrQuestion(0);
        const auto end = pop().source;
        emit(Op::returnValue, end);
    }

    std::size_t raw(Op op, Position source, Expr expr = {})
    {
        program_.code.emplace_back(op, source, std::move(expr));
        return program_.code.size() - 1;
    }

    static bool userCall(const Expr &expr)
    {
        return expr && expr->kind == Kind::call && !builtin(expr->token.text);
    }

    static bool containsCall(const Expr &expr)
    {
        return expr && (userCall(expr) || containsCall(expr->left) || containsCall(expr->right)
            || std::ranges::any_of(expr->args, containsCall));
    }

    Expr lowerCall(Expr expr)
    {
        if (!containsCall(expr))
            return expr;
        const bool assignment = expr->kind == Kind::binary && precedence(expr->token.text) == 1;
        const auto &candidate = assignment ? expr->right : expr;
        if (!userCall(candidate) || (assignment && containsCall(expr->left))
            || std::ranges::any_of(candidate->args, containsCall))
            throw Diagnostic(expr->token.source,
                "Place cet appel dans une instruction ou une affectation dédiée.");
        raw(Op::invoke, candidate->token.source, candidate);
        auto value = expression(Kind::result, candidate->token);
        if (assignment)
            return expression(Kind::binary, expr->token, expr->left, std::move(value));
        return value;
    }

    std::size_t emit(Op op, Position source, Expr expr = {})
    {
        return raw(op, source, lowerCall(std::move(expr)));
    }

    void setTarget(std::size_t branch)
    {
        program_.code[branch].target = program_.code.size();
    }

    Expr parseExpr(int minPriority = 1)
    {
        if (++depth_ > 96)
            fail("Expression trop imbriquée.");
        Expr left = unary();
        while (here().kind == TokenKind::symbol)
        {
            const int priority = precedence(here().text);
            if (priority < minPriority)
                break;
            const Token op = pop();
            if (inProbe_ && priority == 1)
                throw Diagnostic(op.source, "Une règle ne modifie pas le programme.");
            auto right = parseExpr(priority == 1 ? priority : priority + 1);
            if (++expressions_ > 3500)
                fail("Ce programme contient trop d'opérations.");
            left = expression(Kind::binary, op, std::move(left), std::move(right));
        }
        --depth_;
        return left;
    }

    Expr unary()
    {
        if (at("!") || at("&") || at("*") || at("-") || at("+") || at("++") || at("--"))
        {
            if (++depth_ > 96)
                fail("Expression trop imbriquée.");
            const Token op = pop();
            if (inProbe_ && (op.text == "++" || op.text == "--"))
                throw Diagnostic(op.source, "Une règle ne modifie pas le programme.");
            auto value = expression(Kind::unary, op, unary());
            --depth_;
            return value;
        }

        Expr left;
        if (eat("("))
        {
            left = parseExpr();
            need(")");
        }
        else
        {
            Token atom = pop();
            if (atom.kind == TokenKind::identifier)
            {
                if (atom.text == "NULL" || atom.text == "EOF")
                {
                    atom.number = atom.text == "EOF" ? -1 : 0;
                    left = expression(Kind::number, atom);
                }
                else if (eat("("))
                {
                    if (inProbe_)
                        throw Diagnostic(atom.source, "Les règles acceptent des expressions sans appels.");
                    std::vector<Expr> args;
                    if (!at(")"))
                    {
                        do { args.push_back(parseExpr()); } while (eat(","));
                    }
                    need(")");
                    left = expression(Kind::call, atom, {}, {}, std::move(args));
                }
                else
                {
                    while (inProbe_ && eat("."))
                        atom.text += "." + identifier().text;
                    left = expression(Kind::name, atom);
                }
            }
            else if (atom.kind == TokenKind::string)
                left = expression(Kind::string, atom);
            else if (atom.kind == TokenKind::number || atom.kind == TokenKind::character)
                left = expression(Kind::number, atom);
            else
                throw Diagnostic(atom.source, "Expression attendue.");
        }

        while (at("[") || at("++") || at("--"))
        {
            const Token op = pop();
            if (op.text == "[")
            {
                auto index = parseExpr();
                need("]");
                if (left->kind == Kind::name && index->kind == Kind::name)
                {
                    auto pair = std::make_pair(left->token.text, index->token.text);
                    if (std::find(program_.indexUses.begin(), program_.indexUses.end(), pair)
                        == program_.indexUses.end())
                        program_.indexUses.push_back(std::move(pair));
                }
                left = expression(Kind::index, op, std::move(left), std::move(index));
            }
            else
            {
                if (inProbe_)
                    throw Diagnostic(op.source, "Une règle ne modifie pas le programme.");
                left = expression(Kind::post, op, std::move(left));
            }
        }
        return left;
    }

    void declaration()
    {
        const Token base = pop();
        do
        {
            Declaration decl;
            decl.source = base.source;
            decl.type.base = base.text == "char" ? Base::character : Base::integer;
            while (eat("*"))
            {
                if (++decl.type.pointers > 8)
                    fail("Trop de niveaux de pointeurs.");
            }
            if (here().kind != TokenKind::identifier)
                fail("Nom de variable attendu.");
            decl.name = pop().text;
            static constexpr std::string_view reserved[] = {
                "int", "char", "void", "if", "else", "while", "for", "return", "break", "continue",
                "NULL", "EOF", "sizeof", "const", "switch", "do", "main"
            };
            if (builtin(decl.name) || std::ranges::find(reserved, decl.name) != std::end(reserved))
                fail("Nom réservé : " + decl.name);
            if (eat("["))
            {
                decl.array = true;
                if (!at("]"))
                {
                    if (here().kind != TokenKind::number)
                        fail("Indique une taille de tableau entière et littérale.");
                    decl.length = pop().number;
                    if (*decl.length < 1 || *decl.length > maxArray)
                        fail("Taille de tableau admise : 1 à 128 cases.");
                }
                need("]");
            }
            if (eat("="))
            {
                if (decl.array && eat("{"))
                {
                    decl.hasList = true;
                    while (!at("}"))
                    {
                        decl.list.push_back(parseExpr());
                        if (!eat(","))
                            break;
                    }
                    need("}");
                }
                else
                    decl.initializer = parseExpr();
            }
            if (decl.array && !decl.length && !decl.initializer && !decl.hasList)
                fail("Indique la taille ou initialise le tableau.");
            if (std::ranges::any_of(decl.list, containsCall))
                throw Diagnostic(decl.source, "Place les appels dans des affectations après la déclaration.");
            decl.initializer = lowerCall(decl.initializer);
            program_.code[raw(Op::declare, decl.source)].declaration = std::move(decl);
        } while (eat(","));
        need(";");
    }

    void statementOrQuestion(int nesting)
    {
        if (here().kind != TokenKind::question)
        {
            statement(nesting);
            return;
        }
        auto marker = pop();
        try
        {
            auto expressions = Parser(Lexer(marker.text).run()).questionExpressions();
            const auto index = raw(Op::question, marker.source);
            program_.code[index].target = program_.questions.size();
            program_.questions.push_back({marker.source, std::move(marker.text), std::move(expressions)});
        }
        catch (const Diagnostic &error)
        {
            throw Diagnostic(marker.source, "Point question invalide · " + std::string(error.what()));
        }
    }

    void statement(int nesting)
    {
        if (nesting > 64)
            fail("Blocs trop imbriqués.");
        const Token token = here();
        if (token.kind == TokenKind::question)
            fail("Place le point question entre deux instructions, à l'intérieur d'un bloc.");
        if (eat("{"))
        {
            emit(Op::enter, token.source);
            while (!eat("}"))
                statementOrQuestion(nesting + 1);
            emit(Op::leave, token.source);
        }
        else if (eat("if") || eat("while"))
        {
            need("(");
            auto test = parseExpr();
            need(")");
            const auto testEntry = program_.code.size();
            const auto branch = emit(Op::test, token.source, std::move(test));
            program_.code[branch].label = token.text;
            statement(nesting + 1);
            if (token.text == "while")
            {
                const auto back = emit(Op::jump, token.source);
                program_.code[back].target = testEntry;
            }
            else if (eat("else"))
            {
                const auto skip = emit(Op::jump, token.source);
                setTarget(branch);
                statement(nesting + 1);
                setTarget(skip);
                return;
            }
            setTarget(branch);
        }
        else if (eat("for"))
        {
            emit(Op::enter, token.source);
            need("(");
            if (at("int") || at("char"))
                declaration();
            else if (!eat(";"))
            {
                const Position initSource = here().source;
                emit(Op::evaluate, initSource, parseExpr());
                need(";");
            }
            const auto condition = at(";") ? Expr{} : parseExpr();
            need(";");
            const auto testEntry = program_.code.size();
            const auto branch = emit(Op::test, token.source, condition);
            program_.code[branch].label = "for";
            const Position advanceSource = here().source;
            const auto advance = at(")") ? Expr{} : parseExpr();
            need(")");
            statement(nesting + 1);
            if (advance)
                emit(Op::evaluate, advanceSource, advance);
            const auto back = emit(Op::jump, token.source);
            program_.code[back].target = testEntry;
            setTarget(branch);
            emit(Op::leave, token.source);
        }
        else if (eat("return"))
        {
            const auto result = at(";") ? Expr{} : parseExpr();
            emit(Op::returnValue, token.source, result);
            need(";");
        }
        else if (at("int") || at("char"))
            declaration();
        else if (!eat(";"))
        {
            emit(Op::evaluate, token.source, parseExpr());
            need(";");
        }
    }
};

} // namespace

Program compile(std::string_view text, bool withQuestions)
{
    if (text.size() > 32768)
        throw Diagnostic({}, "Limite de 32 Kio par programme.");
    return Parser(Lexer(text, withQuestions).run()).run();
}

Expr compileProbe(std::string_view text)
{
    if (text.empty() || text.size() > 1024)
        throw Diagnostic({}, "Expression de débogage attendue.");
    return Parser(Lexer(text).run()).probe();
}

const Function *Program::function(std::string_view name) const
{
    const auto it = std::ranges::find(functions, name, &Function::name);
    return it == functions.end() ? nullptr : &*it;
}

std::string_view Frame::functionName() const
{
    return calls.empty() ? std::string_view("main") : calls.back().function;
}

std::uint64_t Frame::activation() const
{
    return calls.empty() ? 0 : calls.back().activation;
}

const Object *Frame::find(std::uint64_t address) const
{
    const auto it = std::find_if(objects.begin(), objects.end(),
        [address](const Object &object) { return object.baseAddress == address; });
    return it == objects.end() ? nullptr : &*it;
}

const Object *Frame::find(std::string_view name) const
{
    for (auto it = objects.rbegin(); it != objects.rend(); ++it)
        if (!it->literal && it->activation == activation() && it->name == name)
            return &*it;
    return nullptr;
}

const Object *Frame::qualified(std::string_view name, bool allowRetired, bool &expired) const
{
    const auto dot = name.find('.');
    if (dot == name.npos)
        return find(name);
    const auto function = name.substr(0, dot);
    const auto variable = name.substr(dot + 1);
    auto matches = [&](const Object &obj) { return !obj.literal && obj.function == function && obj.name == variable; };
    for (auto it = objects.rbegin(); it != objects.rend(); ++it)
        if (matches(*it))
            return &*it;
    if (allowRetired)
        for (auto it = retired.rbegin(); it != retired.rend(); ++it)
            if (matches(*it))
            {
                expired = true;
                return &*it;
            }
    return nullptr;
}

std::string Frame::describe(Address address) const
{
    if (!address.object)
        return "NULL";
    const auto *object = find(address.object);
    if (!object)
        return "hors portée";
    if (address.index == static_cast<int>(object->cells.size()))
        return "fin de " + object->name;
    return "&" + object->label(address.index);
}

std::string Frame::hex(Address address) const
{
    const auto *object = find(address.object);
    if (!object && address.object)
        return "—";
    const auto offset = object ? address.index * object->element.bytes() : 0;
    std::ostringstream text;
    text << "0x" << std::uppercase << std::hex << address.object + offset;
    return text.str();
}

std::string charDisplay(int number)
{
    const auto byte = static_cast<unsigned char>(number);
    if (byte == 0) return "'\\0'";
    if (byte == '\r') return "'\\r'";
    if (byte == '\n') return "'\\n'";
    if (byte == '\t') return "'\\t'";
    if (byte == '\\') return "'\\\\'";
    if (byte == '\'') return "'\\''";
    if (byte >= 32 && byte <= 126)
        return std::string("'") + static_cast<char>(byte) + "'";
    std::ostringstream text;
    text << "0x" << std::uppercase << std::hex << static_cast<int>(byte);
    return text.str();
}

std::string valueDisplay(const Frame &frame, const Value &value)
{
    if (value.type.pointers)
        return frame.describe(value.address);
    if (value.type.base == Base::character)
        return charDisplay(value.number);
    return std::to_string(value.number);
}

namespace
{

void retire(Frame &frame, auto matches)
{
    for (auto it = frame.objects.begin(); it != frame.objects.end();)
    {
        if (!matches(*it))
        {
            ++it;
            continue;
        }
        std::erase_if(frame.retired, [&](const Object &old)
            { return old.function == it->function && old.name == it->name; });
        if (frame.retired.size() == maxRetired)
            frame.retired.erase(frame.retired.begin());
        frame.retired.push_back(std::move(*it));
        it = frame.objects.erase(it);
    }
}

class Runtime
{
public:
    Runtime(const Program &program, Frame &frame, std::string_view input = {}, bool eof = false)
        : program_(program), frame_(frame), input_(input), eof_(eof)
    {
    }

    Probe probe(const Expr &expr, bool afterScope)
    {
        inspectRetired_ = afterScope;
        const auto value = evaluate(expr);
        return {valueDisplay(frame_, value), expired_, truth(value),
                value.type.pointers ? std::nullopt : std::optional(value.number)};
    }

    void execute(const Instruction &step)
    {
        using Op = Instruction::Op;
        ++frame_.pc;
        switch (step.op)
        {
        case Op::declare:
            declare(step.declaration);
            frame_.event = "Déclaration de " + step.declaration.name;
            break;
        case Op::evaluate:
            evaluate(step.expression);
            frame_.event = "Instruction exécutée";
            for (const auto &access : frame_.accesses)
                if (access.write)
                {
                    const auto *object = frame_.find(access.address.object);
                    if (object)
                        frame_.event = "Écriture dans " + object->label(access.address.index);
                }
            break;
        case Op::test:
        {
            const bool pass = !step.expression || truth(evaluate(step.expression));
            frame_.testResult = pass;
            if (!pass)
                frame_.pc = step.target;
            frame_.event = "Test " + step.label + (pass ? " vrai" : " faux");
            break;
        }
        case Op::invoke:
            invoke(*step.expression);
            break;
        case Op::returnValue:
            returnFrom(step);
            break;
        case Op::question:
            break;
        default:
            throw std::logic_error("Instruction technique inattendue");
        }
    }

private:
    using Kind = Expression::Kind;
    const Program &program_;
    Frame &frame_;
    std::string_view input_;
    bool eof_;
    bool inspectRetired_ = false;
    bool expired_ = false;

    void invoke(const Expression &expr)
    {
        const auto *function = program_.function(expr.token.text);
        if (!function || function->name == "main")
            throw Diagnostic(expr.token.source, "Fonction inconnue : " + expr.token.text);
        if (function->parameters.size() != expr.args.size())
            throw Diagnostic(expr.token.source, "Nombre d'arguments incorrect pour " + function->name + ".");
        if (frame_.calls.size() == 32)
            throw Diagnostic(expr.token.source, "Profondeur d'appel maximale atteinte.");
        std::vector<Value> arguments;
        for (const auto &arg : expr.args)
            arguments.push_back(evaluate(arg));
        const auto activation = frame_.nextActivation++;
        frame_.calls.push_back({function->name, activation, frame_.pc, frame_.scope});
        frame_.scope = 0;
        frame_.pc = function->entry;
        for (std::size_t i = 0; i < arguments.size(); ++i)
        {
            const auto &parameter = function->parameters[i];
            const auto address = allocate(parameter.name, parameter.type, false, false, 1, expr.token.source);
            write(address, arguments[i], expr.token.source);
        }
        frame_.event = "Appel de " + function->name;
    }

    void returnFrom(const Instruction &step)
    {
        const auto name = std::string(frame_.functionName());
        const auto *function = program_.function(name);
        const bool main = frame_.calls.empty();
        if (!main && ((!step.expression && function->result) || (step.expression && !function->result)))
            throw Diagnostic(step.source, function->result ? "Valeur de retour attendue." : "Cette fonction retourne void.");
        auto result = step.expression ? evaluate(step.expression) : Value::integer(0);
        if (function && function->result)
            result = convert(result, *function->result, step.source);
        frame_.event = "Retour de " + name;
        if (main)
        {
            frame_.returnCode = asInt(result, step.source);
            frame_.finished = true;
            return;
        }
        const auto call = frame_.calls.back();
        retire(frame_, [&](const Object &obj) { return !obj.literal && obj.activation == call.activation; });
        frame_.calls.pop_back();
        frame_.scope = call.callerScope;
        frame_.pc = call.resume;
        frame_.returned = function->result ? std::optional<Value>(result) : std::nullopt;
        // Standalone void calls use the otherwise unused result instruction as their return site.
        if (!function->result && frame_.pc < program_.code.size())
        {
            const auto &next = program_.code[frame_.pc];
            if (next.op == Instruction::Op::evaluate && next.expression && next.expression->kind == Kind::result)
                ++frame_.pc;
        }
    }

    static bool truth(Value value)
    {
        return value.type.pointers ? value.address.object != 0 : value.number != 0;
    }

    static int asInt(Value value, Position source)
    {
        if (value.type.pointers)
            throw Diagnostic(source, "Un entier est attendu ici.");
        return value.number;
    }

    static Value intResult(std::int64_t value, Position source)
    {
        if (value < std::numeric_limits<std::int32_t>::min()
            || value > std::numeric_limits<std::int32_t>::max())
            throw Diagnostic(source, "Le résultat dépasse la plage de int.");
        return Value::integer(static_cast<int>(value));
    }

    static Value convert(Value value, Type type, Position source)
    {
        if (type == value.type)
            return value;
        if (type.pointers)
        {
            if (!value.type.pointers && value.number == 0)
                return Value::pointer(type, {});
            throw Diagnostic(source, "Une adresse de type " + type.name() + " est attendue.");
        }
        if (value.type.pointers)
            throw Diagnostic(source, "Une adresse ne peut pas être stockée dans un " + type.name() + ".");
        if (type.base == Base::character)
        {
            const int byte = static_cast<unsigned char>(value.number);
            value.number = byte < 128 ? byte : byte - 256;
        }
        value.type = type;
        return value;
    }

    Object &objectAt(Address address, Position source)
    {
        if (!address.object)
            throw Diagnostic(source, "Le pointeur vaut NULL.");
        const auto it = std::find_if(frame_.objects.begin(), frame_.objects.end(),
            [address](const Object &object) { return object.baseAddress == address.object; });
        if (it != frame_.objects.end())
            return *it;
        if (inspectRetired_)
        {
            const auto old = std::ranges::find(frame_.retired, address.object, &Object::baseAddress);
            if (old != frame_.retired.end())
            {
                expired_ = true;
                return *old;
            }
        }
        throw Diagnostic(source, "L'objet pointé est sorti de sa portée.");
    }

    Object &cellAt(Address address, Position source)
    {
        auto &object = objectAt(address, source);
        if (address.index < 0 || address.index >= static_cast<int>(object.cells.size()))
            throw Diagnostic(source, object.label(address.index) + " est en dehors des cases de " + object.name + ".");
        return object;
    }

    Value read(Address address, Position source)
    {
        auto &object = cellAt(address, source);
        const auto &cell = object.cells[address.index];
        if (!cell)
            throw Diagnostic(source, object.label(address.index) + " n'a pas encore de valeur.");
        frame_.accesses.push_back({address, false});
        return *cell;
    }

    Value write(Address address, Value value, Position source, bool initializing = false)
    {
        auto &object = cellAt(address, source);
        if (object.literal)
            throw Diagnostic(source, "Un littéral de chaîne est en lecture seule.");
        value = convert(value, object.element, source);
        object.cells[address.index] = value;
        if (!initializing || object.cells.size() < 20)
            frame_.accesses.push_back({address, true});
        return value;
    }

    Address allocate(std::string name, Type type, bool array, bool literal,
                     std::size_t length, Position source)
    {
        std::size_t used = 0;
        for (const auto &object : frame_.objects)
            used += object.cells.size();
        if (!length || length > maxArray || used + length > maxCells)
            throw Diagnostic(source, "Limite de mémoire de l'atelier atteinte.");
        const auto alignment = static_cast<std::uint64_t>(type.bytes());
        const auto base = (frame_.nextAddress + alignment - 1) & ~(alignment - 1);
        frame_.nextAddress = base + alignment * length + 16;
        frame_.objects.push_back({base, std::move(name), type, array, literal, literal ? -1 : frame_.scope,
                                  std::vector<std::optional<Value>>(length),
                                  std::string(frame_.functionName()), frame_.activation()});
        return {base, 0};
    }

    void declare(const Declaration &decl)
    {
        const auto *found = frame_.find(decl.name);
        if (found && found->scope == frame_.scope)
            throw Diagnostic(decl.source, decl.name + " est déjà déclaré dans ce bloc.");
        if (!decl.array)
        {
            const auto address = allocate(decl.name, decl.type, false, false, 1, decl.source);
            if (decl.initializer)
                write(address, evaluate(decl.initializer), decl.source);
            return;
        }

        const bool isText = decl.initializer && decl.initializer->kind == Kind::string;
        if (decl.initializer && (!isText || decl.type != Type{Base::character, 0}))
            throw Diagnostic(decl.source, "Utilise {…} ou une chaîne pour initialiser un tableau de char.");
        const auto text = isText ? std::string_view(decl.initializer->token.text) : std::string_view{};
        const auto filled = isText ? text.size() : decl.list.size();
        const auto length = decl.length ? static_cast<std::size_t>(*decl.length)
                                        : filled + (isText ? 1 : 0);
        if (filled > length)
            throw Diagnostic(decl.source, "Trop de valeurs pour la taille de " + decl.name + ".");
        auto address = allocate(decl.name, decl.type, true, false, length, decl.source);
        if (decl.hasList || isText)
        {
            for (std::size_t i = 0; i < length; ++i)
            {
                address.index = static_cast<int>(i);
                Value value = Value::integer(0);
                if (isText && i < text.size())
                    value = Value::integer(static_cast<unsigned char>(text[i]));
                else if (i < decl.list.size())
                    value = evaluate(decl.list[i]);
                write(address, value, decl.source, true);
            }
        }
    }

    Address offset(Value pointer, int amount, Position source)
    {
        if (!pointer.type.pointers)
            throw Diagnostic(source, "Un tableau ou un pointeur est attendu.");
        const auto &object = objectAt(pointer.address, source);
        const auto index = static_cast<std::int64_t>(pointer.address.index) + amount;
        if (index < 0 || index > static_cast<std::int64_t>(object.cells.size()))
            throw Diagnostic(source, "Indice " + std::to_string(index) + " hors limites pour " + object.name
                + " (" + std::to_string(object.cells.size()) + " cases).");
        return {pointer.address.object, static_cast<int>(index)};
    }

    const Object &variable(const Token &token)
    {
        if (const auto *object = frame_.qualified(token.text, inspectRetired_, expired_))
            return *object;
        throw Diagnostic(token.source, "Nom inconnu : " + token.text);
    }

    Address lvalue(const Expr &expr, bool takingAddress = false)
    {
        const auto &token = expr->token;
        if (expr->kind == Kind::name)
        {
            const auto &object = variable(token);
            if (object.array)
                throw Diagnostic(token.source, "Utilise une case du tableau, comme " + object.name + "[0].");
            return {object.baseAddress, 0};
        }
        if (expr->kind == Kind::index)
        {
            Value array = evaluate(expr->left);
            const int index = asInt(evaluate(expr->right), token.source);
            return offset(array, index, token.source);
        }
        if (expr->kind == Kind::unary && token.text == "*")
        {
            const Value pointer = evaluate(expr->left);
            if (!pointer.type.pointers)
                throw Diagnostic(token.source, "* attend un pointeur.");
            if (!takingAddress)
                cellAt(pointer.address, token.source);
            return pointer.address;
        }
        throw Diagnostic(token.source, "Une variable ou une case est attendue ici.");
    }

    Value arithmetic(std::string_view op, Value left, Value right, Position source)
    {
        if (op == "==" || op == "!=")
        {
            bool equal = false;
            if (!left.type.pointers && !right.type.pointers)
                equal = left.number == right.number;
            else if (left.type.pointers && right.type.pointers)
            {
                if (left.type != right.type)
                    throw Diagnostic(source, "Les pointeurs comparés doivent avoir le même type.");
                equal = left.address == right.address;
            }
            else
            {
                const auto &pointer = left.type.pointers ? left : right;
                const auto &number = left.type.pointers ? right : left;
                if (number.number != 0)
                    throw Diagnostic(source, "Un pointeur peut être comparé à NULL ou à un pointeur du même type.");
                equal = pointer.address.object == 0;
            }
            return Value::integer(op == "==" ? equal : !equal);
        }
        if (left.type.pointers || right.type.pointers)
        {
            if (op == "+" && !left.type.pointers && right.type.pointers)
                std::swap(left, right);
            if ((op == "+" || op == "-") && left.type.pointers && !right.type.pointers)
            {
                const auto amount = op == "+" ? right.number
                    : intResult(-static_cast<std::int64_t>(right.number), source).number;
                return Value::pointer(left.type, offset(left, amount, source));
            }
            if (left.type.pointers && left.type == right.type && left.address.object != 0
                && left.address.object == right.address.object)
            {
                objectAt(left.address, source);
                const int lhs = left.address.index;
                const int rhs = right.address.index;
                if (op == "-") return Value::integer(lhs - rhs);
                if (op == "<") return Value::integer(lhs < rhs);
                if (op == ">") return Value::integer(lhs > rhs);
                if (op == "<=") return Value::integer(lhs <= rhs);
                if (op == ">=") return Value::integer(lhs >= rhs);
            }
            throw Diagnostic(source, "Opération non définie sur ces adresses.");
        }

        const std::int64_t lhs = left.number;
        const std::int64_t rhs = right.number;
        if (op == "+") return intResult(lhs + rhs, source);
        if (op == "-") return intResult(lhs - rhs, source);
        if (op == "*") return intResult(lhs * rhs, source);
        if (op == "<") return Value::integer(lhs < rhs);
        if (op == ">") return Value::integer(lhs > rhs);
        if (op == "<=") return Value::integer(lhs <= rhs);
        if (op == ">=") return Value::integer(lhs >= rhs);
        if (rhs == 0)
            throw Diagnostic(source, "Division par zéro.");
        if (lhs == std::numeric_limits<std::int32_t>::min() && rhs == -1)
            throw Diagnostic(source, "Le quotient dépasse la plage de int.");
        return intResult(op == "/" ? lhs / rhs : lhs % rhs, source);
    }

    Value stringLiteral(const Token &token)
    {
        const Type character{Base::character, 0};
        for (const auto &object : frame_.objects)
        {
            if (!object.literal || object.cells.size() != token.text.size() + 1)
                continue;
            bool same = true;
            for (std::size_t i = 0; i < token.text.size(); ++i)
                same &= static_cast<unsigned char>(object.cells[i]->number)
                    == static_cast<unsigned char>(token.text[i]);
            if (same)
                return Value::pointer(character.pointerTo(), {object.baseAddress, 0});
        }
        const auto address = allocate("littéral", character, true, true, token.text.size() + 1, token.source);
        auto &cells = frame_.objects.back().cells;
        for (std::size_t i = 0; i < cells.size(); ++i)
        {
            const int byte = i == token.text.size() ? 0 : static_cast<unsigned char>(token.text[i]);
            cells[i] = Value{character, byte < 128 ? byte : byte - 256, {}};
        }
        return Value::pointer(character.pointerTo(), address);
    }

    std::string readString(const Expr &expr)
    {
        if (expr->kind == Kind::string)
            return std::string(expr->token.text.c_str());
        auto pointer = evaluate(expr);
        if (pointer.type != Type{Base::character, 1})
            throw Diagnostic(expr->token.source, "Une chaîne (char *) est attendue.");
        std::string text;
        for (auto address = pointer.address;; ++address.index)
        {
            const auto value = read(address, expr->token.source);
            if (!value.number)
                return text;
            text += static_cast<char>(value.number);
        }
    }

    bool available(std::size_t at) const
    {
        if (at < input_.size())
            return true;
        if (!eof_)
            throw NeedsInput{};
        return false;
    }

    void skipInputSpace(std::size_t &at) const
    {
        while (available(at) && std::isspace(static_cast<unsigned char>(input_[at])))
            ++at;
    }

    void consume(std::size_t end)
    {
        frame_.inputOffset = end;
    }

    Value scan(const Expression &expr)
    {
        const auto format = readString(expr.args[0]);
        std::size_t at = frame_.inputOffset;
        std::size_t arg = 1;
        int assigned = 0;
        bool hitEof = false;
        const Position source = expr.token.source;
        for (std::size_t i = 0; i < format.size(); ++i)
        {
            if (std::isspace(static_cast<unsigned char>(format[i])))
            {
                skipInputSpace(at);
                continue;
            }
            if (format[i] != '%' || (i + 1 < format.size() && format[i + 1] == '%'))
            {
                hitEof = !available(at);
                if (hitEof || input_[at] != format[i])
                    break;
                ++at;
                if (format[i] == '%')
                    ++i;
                continue;
            }
            if (++i >= format.size() || (format[i] != 'd' && format[i] != 'c' && format[i] != 's'))
                throw Diagnostic(source, "Formats scanf disponibles : %d, %c, %s et %%.");
            if (arg >= expr.args.size())
                throw Diagnostic(source, "Argument manquant dans scanf.");
            const auto dest = evaluate(expr.args[arg++]);
            const Type expected{format[i] == 'd' ? Base::integer : Base::character, 1};
            if (dest.type != expected)
                throw Diagnostic(source, "scanf attend une adresse de type " + expected.name() + ".");
            if (format[i] != 'c')
                skipInputSpace(at);
            if (!available(at))
            {
                hitEof = true;
                break;
            }
            if (format[i] == 'c')
            {
                write(dest.address, Value::integer(static_cast<unsigned char>(input_[at++])), source);
            }
            else if (format[i] == 's')
            {
                const auto begin = at;
                while (available(at) && !std::isspace(static_cast<unsigned char>(input_[at])))
                    ++at;
                auto cell = dest.address;
                for (std::size_t cursor = begin; cursor < at; ++cursor, ++cell.index)
                    write(cell, Value::integer(static_cast<unsigned char>(input_[cursor])), source);
                write(cell, Value::integer(0), source);
            }
            else
            {
                auto end = at;
                const bool negative = input_[end] == '-';
                if (input_[end] == '+' || negative)
                    ++end;
                if (!available(end) || !digit(input_[end]))
                    break;
                std::int64_t integer = 0;
                while (available(end) && digit(input_[end]))
                {
                    integer = integer * 10 + (input_[end++] - '0');
                    intResult(negative ? -integer : integer, source);
                }
                write(dest.address, intResult(negative ? -integer : integer, source), source);
                at = end;
            }
            ++assigned;
        }
        consume(at);
        return Value::integer(hitEof && assigned == 0 ? -1 : assigned);
    }

    std::string formatOutput(const Expression &expr, std::size_t formatIndex)
    {
        const auto &args = expr.args;
        const auto &name = expr.token.text;
        const auto source = expr.token.source;
        const auto format = readString(args[formatIndex]);
        std::size_t arg = formatIndex + 1;
        std::string text;
        for (std::size_t i = 0; i < format.size(); ++i)
        {
            if (format[i] != '%')
            {
                text += format[i];
                continue;
            }
            if (++i == format.size())
                throw Diagnostic(source, "Format " + name + " incomplet.");
            if (format[i] == '%')
            {
                text += '%';
                continue;
            }
            if (arg >= args.size())
                throw Diagnostic(source, "Argument manquant dans " + name + ".");
            const auto &value = args[arg++];
            if (format[i] == 's') text += readString(value);
            else if (format[i] == 'c') text += static_cast<char>(asInt(evaluate(value), source));
            else if (format[i] == 'd') text += std::to_string(asInt(evaluate(value), source));
            else if (format[i] == 'p')
            {
                const auto address = evaluate(value);
                if (!address.type.pointers)
                    throw Diagnostic(source, "%p attend un pointeur.");
                text += frame_.hex(address.address);
            }
            else
                throw Diagnostic(source, "Formats disponibles dans " + name + " : %d, %c, %s, %p et %%.");
        }
        if (arg != args.size())
            throw Diagnostic(source, "Il reste des arguments sans format dans " + name + ".");
        return text;
    }

    bool writesToStderr(const Expr &stream) const
    {
        if (stream->kind == Kind::name && !frame_.find(stream->token.text))
        {
            if (stream->token.text == "stdout")
                return false;
            if (stream->token.text == "stderr")
                return true;
        }
        throw Diagnostic(stream->token.source, "Indique stdout ou stderr comme flux de sortie.");
    }

    void writeOutput(std::string_view text, bool toStderr, Position source)
    {
        auto &output = toStderr ? frame_.errorOutput : frame_.output;
        if (output.size() + text.size() > maxOutput)
            throw Diagnostic(source, toStderr ? "La sortie stderr dépasse 4 096 octets."
                                               : "La sortie dépasse 4 096 octets.");
        output += text;
        if (!toStderr)
            frame_.terminal += text;
    }

    Value call(const Expression &expr)
    {
        const auto &args = expr.args;
        const auto &token = expr.token;
        const auto &name = token.text;
        if (name == "getchar" && args.empty())
        {
            if (!available(frame_.inputOffset))
                return Value::integer(-1);
            const auto byte = static_cast<unsigned char>(input_[frame_.inputOffset]);
            consume(frame_.inputOffset + 1);
            return Value::integer(byte);
        }
        if (name == "scanf" && !args.empty())
            return scan(expr);

        bool toStderr = false;
        std::string text;
        const bool character = (name == "putchar" && args.size() == 1)
            || ((name == "fputc" || name == "putc") && args.size() == 2);
        if (character)
        {
            if (args.size() == 2)
                toStderr = writesToStderr(args[1]);
            text += static_cast<char>(asInt(evaluate(args[0]), token.source));
        }
        else if (name == "puts" && args.size() == 1)
            text = readString(args[0]) + "\n";
        else if (name == "fputs" && args.size() == 2)
        {
            toStderr = writesToStderr(args[1]);
            text = readString(args[0]);
        }
        else if (name == "printf" && !args.empty())
            text = formatOutput(expr, 0);
        else if (name == "fprintf" && args.size() >= 2)
        {
            toStderr = writesToStderr(args[0]);
            text = formatOutput(expr, 1);
        }
        else
            throw Diagnostic(token.source, "Appel non pris en charge : " + name + ".");
        writeOutput(text, toStderr, token.source);
        return Value::integer(character ? static_cast<unsigned char>(text.front())
                                        : static_cast<int>(text.size()));
    }

    Value evaluate(const Expr &expr)
    {
        const auto &token = expr->token;
        switch (expr->kind)
        {
        case Kind::number:
            return Value::integer(token.number);
        case Kind::result:
        {
            if (!frame_.returned)
                throw Diagnostic(token.source, "Une fonction void ne produit pas de valeur.");
            const auto result = *frame_.returned;
            frame_.returned.reset();
            return result;
        }
        case Kind::string:
            return stringLiteral(token);
        case Kind::name:
        {
            const auto &object = variable(token);
            const Address address{object.baseAddress, 0};
            if (object.array)
                return Value::pointer(object.element.pointerTo(), address);
            return read(address, token.source);
        }
        case Kind::index:
            return read(lvalue(expr), token.source);
        case Kind::call:
            return call(*expr);
        case Kind::post:
        case Kind::unary:
        {
            if (token.text == "++" || token.text == "--")
            {
                const auto address = lvalue(expr->left);
                const auto before = read(address, token.source);
                const auto after = arithmetic("+", before, Value::integer(token.text == "++" ? 1 : -1), token.source);
                const auto stored = write(address, after, token.source);
                return expr->kind == Kind::post ? before : stored;
            }
            if (token.text == "&")
            {
                const auto &operand = expr->left;
                if (operand->kind == Kind::unary && operand->token.text == "*")
                {
                    const auto pointer = evaluate(operand->left);
                    if (!pointer.type.pointers)
                        throw Diagnostic(token.source, "&* attend un pointeur.");
                    return pointer;
                }
                const auto address = lvalue(operand, true);
                return Value::pointer(objectAt(address, token.source).element.pointerTo(), address);
            }
            if (token.text == "*")
                return read(lvalue(expr), token.source);
            const auto value = evaluate(expr->left);
            if (token.text == "!")
                return Value::integer(!truth(value));
            const std::int64_t number = asInt(value, token.source);
            return intResult(token.text == "-" ? -number : number, token.source);
        }
        case Kind::binary:
        {
            const auto &op = token.text;
            if (precedence(op) == 1)
            {
                const auto dest = lvalue(expr->left);
                const auto right = evaluate(expr->right);
                if (op == "=")
                    return write(dest, right, token.source);
                const auto left = read(dest, token.source);
                return write(dest, arithmetic(std::string_view(op).substr(0, 1), left, right, token.source), token.source);
            }
            const auto left = evaluate(expr->left);
            if (op == "&&")
                return Value::integer(truth(left) && truth(evaluate(expr->right)));
            if (op == "||")
                return Value::integer(truth(left) || truth(evaluate(expr->right)));
            return arithmetic(op, left, evaluate(expr->right), token.source);
        }
        }
        throw std::logic_error("Expression inattendue");
    }
};

void settle(const Program &program, Frame &frame)
{
    using Op = Instruction::Op;
    while (frame.pc < program.code.size() && !frame.finished)
    {
        const auto &step = program.code[frame.pc];
        if (step.op == Op::enter)
        {
            ++frame.scope;
            ++frame.pc;
        }
        else if (step.op == Op::leave)
        {
            retire(frame, [&](const Object &object)
                { return object.activation == frame.activation() && object.scope == frame.scope; });
            --frame.scope;
            ++frame.pc;
        }
        else if (step.op == Op::jump)
            frame.pc = step.target;
        else
            return;
    }
    if (!frame.finished)
    {
        frame.finished = true;
        frame.returnCode = 0;
        if (frame.steps == 0)
            frame.event = "Le programme est vide";
    }
}

} // namespace

Machine::Machine(Program program, bool keepHistory)
    : program_(std::move(program)), historyLimit_(keepHistory ? maxHistory : 1)
{
    reset();
}

void Machine::reset()
{
    history_.clear();
    cursor_ = 0;
    auto &frame = history_.emplace_back();
    waiting_ = false;
    input_.clear();
    frame.pc = program_.entry;
    frame.event = "Prêt à exécuter";
    settle(program_, frame);
}

bool Machine::next()
{
    waiting_ = false;
    const auto &before = frame();
    if (before.finished || before.error)
        return false;
    if (cursor_ + 1 < history_.size())
    {
        ++cursor_;
        return true;
    }

    auto after = before;
    after.lastLine = nextLine();
    ++after.steps;
    after.accesses.clear();
    after.testResult.reset();
    try
    {
        Runtime(program_, after, input(), before.inputEof).execute(program_.code[after.pc]);
        settle(program_, after);
    }
    catch (const NeedsInput &)
    {
        waiting_ = true;
        return false;
    }
    catch (const Diagnostic &error)
    {
        after = before;
        ++after.steps;
        after.lastLine = nextLine();
        after.accesses.clear();
        after.testResult.reset();
        after.error = error;
        after.event = "Exécution arrêtée";
    }
    history_.push_back(std::move(after));
    ++cursor_;
    if (history_.size() > historyLimit_)
    {
        history_.pop_front();
        --cursor_;
    }
    return true;
}

bool Machine::previous()
{
    if (!canPrevious())
        return false;
    --cursor_;
    waiting_ = false;
    return true;
}

bool Machine::submit(std::string_view bytes)
{
    if (inputClosed() || frame().inputEnd + bytes.size() > maxInput)
        return false;
    // Giving new input after reversing a step branches the execution.
    history_.resize(cursor_ + 1);
    auto &current = history_.back();
    input_.resize(current.inputEnd);
    input_ += bytes;
    current.inputEnd = input_.size();
    current.terminal += bytes;
    waiting_ = false;
    return true;
}

void Machine::closeInput()
{
    history_.resize(cursor_ + 1);
    input_.resize(frame().inputEnd);
    history_.back().inputEof = true;
    waiting_ = false;
}

std::optional<Probe> Machine::inspect(const Expr &expression, bool afterScope) const
{
    try
    {
        auto scratch = frame();
        return Runtime(program_, scratch).probe(expression, afterScope);
    }
    catch (const Diagnostic &)
    {
        return std::nullopt;
    }
}

const Question *Machine::pendingQuestion() const
{
    const auto &state = frame();
    if (state.finished || state.error || state.pc >= program_.code.size())
        return nullptr;
    const auto &step = program_.code[state.pc];
    return step.op == Instruction::Op::question ? &program_.questions[step.target] : nullptr;
}

Position Machine::nextPosition() const
{
    if (frame().error)
        return frame().error->where;
    if (frame().finished)
        return {0, 0};
    return program_.code[frame().pc].source;
}

} // namespace cmini
