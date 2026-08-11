window.SIN_BLOCKS = {
  "imports": [],
  "libs": [],
  "libImpl": [],
  "structs": [],
  "globals": [],
  "program": [
    {
      "block": "fn",
      "name": "fib",
      "params": [
        {
          "name": "n",
          "type": "int",
          "len": 0
        }
      ],
      "ret": "int",
      "retLen": 0,
      "body": [
        {
          "line": 3,
          "block": "if",
          "cond": {
            "line": 3,
            "block": "binary",
            "op": "<",
            "lhs": {
              "line": 3,
              "block": "var",
              "name": "n"
            },
            "rhs": {
              "line": 3,
              "block": "int",
              "value": 2
            }
          },
          "then": [
            {
              "line": 4,
              "block": "return",
              "value": {
                "line": 4,
                "block": "var",
                "name": "n"
              }
            }
          ]
        },
        {
          "line": 6,
          "block": "return",
          "value": {
            "line": 6,
            "block": "binary",
            "op": "+",
            "lhs": {
              "line": 6,
              "block": "call",
              "callee": "fib",
              "args": [
                {
                  "line": 6,
                  "block": "binary",
                  "op": "-",
                  "lhs": {
                    "line": 6,
                    "block": "var",
                    "name": "n"
                  },
                  "rhs": {
                    "line": 6,
                    "block": "int",
                    "value": 1
                  }
                }
              ]
            },
            "rhs": {
              "line": 6,
              "block": "call",
              "callee": "fib",
              "args": [
                {
                  "line": 6,
                  "block": "binary",
                  "op": "-",
                  "lhs": {
                    "line": 6,
                    "block": "var",
                    "name": "n"
                  },
                  "rhs": {
                    "line": 6,
                    "block": "int",
                    "value": 2
                  }
                }
              ]
            }
          }
        }
      ],
      "pre": [" fib.sin — 斐波那契：同时验证递归与迭代两种写法"]
    },
    {
      "block": "fn",
      "name": "fib_iter",
      "params": [
        {
          "name": "n",
          "type": "int",
          "len": 0
        }
      ],
      "ret": "int",
      "retLen": 0,
      "body": [
        {
          "line": 10,
          "block": "let",
          "name": "a",
          "type": "int",
          "len": 0,
          "value": {
            "line": 10,
            "block": "int",
            "value": 0
          }
        },
        {
          "line": 11,
          "block": "let",
          "name": "b",
          "type": "int",
          "len": 0,
          "value": {
            "line": 11,
            "block": "int",
            "value": 1
          }
        },
        {
          "line": 12,
          "block": "let",
          "name": "i",
          "type": "int",
          "len": 0,
          "value": {
            "line": 12,
            "block": "int",
            "value": 0
          }
        },
        {
          "line": 13,
          "block": "while",
          "cond": {
            "line": 13,
            "block": "binary",
            "op": "<",
            "lhs": {
              "line": 13,
              "block": "var",
              "name": "i"
            },
            "rhs": {
              "line": 13,
              "block": "var",
              "name": "n"
            }
          },
          "body": [
            {
              "line": 14,
              "block": "let",
              "name": "t",
              "type": "int",
              "len": 0,
              "value": {
                "line": 14,
                "block": "binary",
                "op": "+",
                "lhs": {
                  "line": 14,
                  "block": "var",
                  "name": "a"
                },
                "rhs": {
                  "line": 14,
                  "block": "var",
                  "name": "b"
                }
              }
            },
            {
              "line": 15,
              "block": "assign",
              "name": "a",
              "value": {
                "line": 15,
                "block": "var",
                "name": "b"
              }
            },
            {
              "line": 16,
              "block": "assign",
              "name": "b",
              "value": {
                "line": 16,
                "block": "var",
                "name": "t"
              }
            },
            {
              "line": 17,
              "block": "assign",
              "name": "i",
              "value": {
                "line": 17,
                "block": "binary",
                "op": "+",
                "lhs": {
                  "line": 17,
                  "block": "var",
                  "name": "i"
                },
                "rhs": {
                  "line": 17,
                  "block": "int",
                  "value": 1
                }
              }
            }
          ]
        },
        {
          "line": 19,
          "block": "return",
          "value": {
            "line": 19,
            "block": "var",
            "name": "a"
          }
        }
      ]
    },
    {
      "block": "fn",
      "name": "main",
      "params": [],
      "ret": "int",
      "retLen": 0,
      "body": [
        {
          "line": 23,
          "block": "let",
          "name": "n",
          "type": "int",
          "len": 0,
          "value": {
            "line": 23,
            "block": "int",
            "value": 10
          }
        },
        {
          "line": 24,
          "block": "expr",
          "expr": {
            "line": 24,
            "block": "call",
            "callee": "print",
            "args": [
              {
                "line": 24,
                "block": "call",
                "callee": "fib",
                "args": [
                  {
                    "line": 24,
                    "block": "var",
                    "name": "n"
                  }
                ]
              }
            ]
          },
          "tail": " 递归: 55"
        },
        {
          "line": 25,
          "block": "expr",
          "expr": {
            "line": 25,
            "block": "call",
            "callee": "print",
            "args": [
              {
                "line": 25,
                "block": "call",
                "callee": "fib_iter",
                "args": [
                  {
                    "line": 25,
                    "block": "var",
                    "name": "n"
                  }
                ]
              }
            ]
          },
          "tail": " 迭代: 55"
        },
        {
          "line": 26,
          "block": "return",
          "value": {
            "line": 26,
            "block": "int",
            "value": 0
          }
        }
      ]
    }
  ]
};
