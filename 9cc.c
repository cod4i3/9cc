#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// トークンの種類を表す列挙体
typedef enum {
	TK_RESERVED, // 記号
	TK_NUM, // 整数トークン
	TK_EOF, // 入力の終わりを表すトークン
} TokenKind;

typedef struct Token Token;

// トークン型
struct Token {
	TokenKind kind; // トークンの型
	Token *next; //次の入力トークン
	int val; // KindがTK_NUM, つまり整数の場合その数値
	char *str; // トークン文字列
};

// 入力したプログラム
char *user_input;

// 現在着目しているトークン
Token *token;

// エラー報告関数
// printfと同じ引数 
void error_at(char *loc, char *fmt, ...) {
	va_list ap;
    va_start(ap, fmt);

    int pos = loc - user_input;
    fprintf(stderr, "%s\n", user_input);
    fprintf(stderr, "%*s", pos, ""); // 位置の表示
    fprintf(stderr, "^ ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	exit(1);
}

// 次のトークンが期待している記号の時は一つ読み進める
// それ以外の場合は false を返す
bool consume(char op) {
	if (token->kind != TK_RESERVED || token->str[0] != op)
		return false;
	token = token->next;
	return true;
}

// 次のトークンが期待している記号のときには一つ読み進める
// それ以外の場合はエラーを報告
void expect(char op) {
	if (token->kind != TK_RESERVED || token->str[0] != op)
		error_at(token->str, "'%c'ではありません", op);
	token = token->next;
}

// 次のトークンが数値の場合、一つ読み進めてその数値を返す
// それ以外の場合にはエラーを報告する
int expect_number() {
	if (token->kind != TK_NUM)
		error_at(token->str, "数ではありません");
	int val = token->val;
	token = token->next;
	return val;
}

// EOFに到達した場合
bool at_eof() {
	return token->kind == TK_EOF;
}

// 新しいトークンを作成してcurに繋げる
Token *new_token(TokenKind kind, Token *cur, char *str) {
	Token *tok = calloc(1, sizeof(Token));
	tok->kind = kind;
	tok->str = str;
	cur->next = tok;
	return tok;
}

// 入力文字列pをトークナイズしてそれを返す
Token *tokenize() {
    char *p = user_input;
	Token head;
	head.next = NULL;
	Token *cur = &head;

	while (*p) {
		//空白文字をスキップ
		if (isspace(*p)) {
			p++;
			continue;
		}

		if (strchr("+-*/()", *p)) {
			cur = new_token(TK_RESERVED, cur, p++);
			continue;
		}

		if (isdigit(*p)) {
			cur = new_token(TK_NUM, cur, p);
			cur->val = strtol(p, &p, 10);
			continue;
		}

		error_at(p, "トークナイズ出来ません");
	}

	new_token(TK_EOF, cur, p);
	return head.next;
}


//　抽象構文木のノードの種類
typedef enum {
    ND_ADD, // +
    ND_SUB, // -
    ND_MUL, // *
    ND_DIV, // /
    ND_NUM, // 整数
} NodeKind;

typedef struct Node Node;

// 抽象構文木のノードの型
struct Node {
    NodeKind kind; // ノードの型
    Node *lhs; // 左辺
    Node *rhs; // 右辺
    int val;   // kindがND_NUMの場合のみ使う
};

Node *new_node(NodeKind kind) {
    Node *node = calloc(1, sizeof(Node));
    node->kind = kind;
    return node;
}

// 新しいノードを受け取る関数
Node *new_binary(NodeKind kind, Node *lhs, Node *rhs) {
    Node *node = new_node(kind);
    node->lhs = lhs;
    node->rhs = rhs;
    return node;
}

Node *new_num(int val) {
    Node *node = new_node(ND_NUM);
    node->val = val;
    return node;
}

Node *expr();
Node *mul();
Node *primary();

// パーサ
// expr = mul("+" mul | "-" mul)
Node *expr() {
    Node *node = mul();

    while(true) {
        if (consume('+'))
            node = new_binary(ND_ADD, node, mul());
        else if (consume('-'))
            node = new_binary(ND_SUB, node, mul());
        else 
            return node;
    }
}

// mul = primary ("*" primary | "/" primary)
Node *mul() {
    Node *node = primary();

    while(true) {
        if (consume('*'))
            node = new_binary(ND_MUL, node, primary());
        else if (consume('/'))
            node = new_binary(ND_DIV, node, primary());
        else return node;
    }
}

// primary = "(" expr ")" | num
Node *primary() {
    // 次のトークンが"("なら "(" expr ")"のはず
    if (consume('(')) {
        Node *node = expr();
        expect(')');
        return node;
    }

    // カッコ以外なら数値
    return new_num(expect_number());
}

// コード生成機
void gen(Node *node) {
    if (node->kind == ND_NUM) {
        printf("    push %d\n", node->val);
        return;
    }

    gen(node->lhs);
    gen(node->rhs);

    printf("    pop rdi\n");
    printf("    pop rax\n");

    switch (node -> kind) {
        case ND_ADD:
            printf("    add rax, rdi\n");
            break;
        case ND_SUB:
            printf("    sub rax, rdi\n");
            break;
        case ND_MUL:
            printf("    imul rax, rdi\n");
            break;
        case ND_DIV:
            printf("    cqo\n");
            printf("    idiv rdi\n");
            break;
    }
    
    printf("    push rax\n");
}


int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "引数の個数は2個です\n");
		return 1;
	}

    user_input = argv[1];
    token = tokenize(user_input);
    Node *node = expr();

	// アセンブリ前半部
	printf(".intel_syntax noprefix\n");
	printf(".global main\n");
	printf("main:\n");

    // 抽象構文木を下りながらコード生成
    gen(node);

    // スタックトップに式全体の値が存在しているはず
    // それをRAXにロードして関数からの返り値とする
    printf("    pop rax\n");
	printf("	ret \n");
	return 0;
}
