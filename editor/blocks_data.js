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
          "block": "if",
          "cond": {
            "block": "binary",
            "op": "<",
            "lhs": {
              "block": "var",
              "name": "n"
            },
            "rhs": {
              "block": "int",
              "value": 2
            }
          },
          "then": [
            {
              "block": "return",
              "value": {
                "block": "var",
                "name": "n"
              }
            }
          ]
        },
        {
          "block": "return",
          "value": {
            "block": "binary",
            "op": "+",
            "lhs": {
              "block": "call",
              "callee": "fib",
              "args": [
                {
                  "block": "binary",
                  "op": "-",
                  "lhs": {
                    "block": "var",
                    "name": "n"
                  },
                  "rhs": {
                    "block": "int",
                    "value": 1
                  }
                }
              ]
            },
            "rhs": {
              "block": "call",
              "callee": "fib",
              "args": [
                {
                  "block": "binary",
                  "op": "-",
                  "lhs": {
                    "block": "var",
                    "name": "n"
                  },
                  "rhs": {
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
          "block": "let",
          "name": "a",
          "type": "int",
          "len": 0,
          "value": {
            "block": "int",
            "value": 0
          }
        },
        {
          "block": "let",
          "name": "b",
          "type": "int",
          "len": 0,
          "value": {
            "block": "int",
            "value": 1
          }
        },
        {
          "block": "let",
          "name": "i",
          "type": "int",
          "len": 0,
          "value": {
            "block": "int",
            "value": 0
          }
        },
        {
          "block": "while",
          "cond": {
            "block": "binary",
            "op": "<",
            "lhs": {
              "block": "var",
              "name": "i"
            },
            "rhs": {
              "block": "var",
              "name": "n"
            }
          },
          "body": [
            {
              "block": "let",
              "name": "t",
              "type": "int",
              "len": 0,
              "value": {
                "block": "binary",
                "op": "+",
                "lhs": {
                  "block": "var",
                  "name": "a"
                },
                "rhs": {
                  "block": "var",
                  "name": "b"
                }
              }
            },
            {
              "block": "assign",
              "name": "a",
              "value": {
                "block": "var",
                "name": "b"
              }
            },
            {
              "block": "assign",
              "name": "b",
              "value": {
                "block": "var",
                "name": "t"
              }
            },
            {
              "block": "assign",
              "name": "i",
              "value": {
                "block": "binary",
                "op": "+",
                "lhs": {
                  "block": "var",
                  "name": "i"
                },
                "rhs": {
                  "block": "int",
                  "value": 1
                }
              }
            }
          ]
        },
        {
          "block": "return",
          "value": {
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
          "block": "let",
          "name": "n",
          "type": "int",
          "len": 0,
          "value": {
            "block": "int",
            "value": 10
          }
        },
        {
          "block": "expr",
          "expr": {
            "block": "call",
            "callee": "print",
            "args": [
              {
                "block": "call",
                "callee": "fib",
                "args": [
                  {
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
          "block": "expr",
          "expr": {
            "block": "call",
            "callee": "print",
            "args": [
              {
                "block": "call",
                "callee": "fib_iter",
                "args": [
                  {
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
          "block": "return",
          "value": {
            "block": "int",
            "value": 0
          }
        }
      ]
    }
  ]
};
