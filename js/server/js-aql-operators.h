static string JS_server_aql_operators = 
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief AQL query language operators (internal use only)\n"
  "///\n"
  "/// @file\n"
  "///\n"
  "/// DISCLAIMER\n"
  "///\n"
  "/// Copyright 2010-2012 triagens GmbH, Cologne, Germany\n"
  "///\n"
  "/// Licensed under the Apache License, Version 2.0 (the \"License\");\n"
  "/// you may not use this file except in compliance with the License.\n"
  "/// You may obtain a copy of the License at\n"
  "///\n"
  "///     http://www.apache.org/licenses/LICENSE-2.0\n"
  "///\n"
  "/// Unless required by applicable law or agreed to in writing, software\n"
  "/// distributed under the License is distributed on an \"AS IS\" BASIS,\n"
  "/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.\n"
  "/// See the License for the specific language governing permissions and\n"
  "/// limitations under the License.\n"
  "///\n"
  "/// Copyright holder is triAGENS GmbH, Cologne, Germany\n"
  "///\n"
  "/// @author Jan Steemann\n"
  "/// @author Copyright 2012, triAGENS GmbH, Cologne, Germany\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "var AQL_TYPEWEIGHT_UNDEFINED = 0;\n"
  "var AQL_TYPEWEIGHT_NULL      = 1;\n"
  "var AQL_TYPEWEIGHT_BOOL      = 2;\n"
  "var AQL_TYPEWEIGHT_NUMBER    = 3;\n"
  "var AQL_TYPEWEIGHT_STRING    = 4;\n"
  "var AQL_TYPEWEIGHT_ARRAY     = 5;\n"
  "var AQL_TYPEWEIGHT_OBJECT    = 6;\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief return the numeric value or undefined if it is out of range\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_NUMERIC_VALUE (lhs) {\n"
  "  if (isNaN(lhs) || !isFinite(lhs)) {\n"
  "    return undefined;\n"
  "  }\n"
  "\n"
  "  return lhs;\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief get the sort type of an operand\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_TYPEWEIGHT (lhs) {\n"
  "  if (lhs === undefined) {\n"
  "    return AQL_TYPEWEIGHT_UNDEFINED;\n"
  "  }\n"
  "\n"
  "  if (lhs === null) {\n"
  "    return AQL_TYPEWEIGHT_NULL;\n"
  "  }\n"
  "\n"
  "  if (lhs instanceof Array) {\n"
  "    return AQL_TYPEWEIGHT_ARRAY;\n"
  "  }\n"
  "\n"
  "  switch (typeof(lhs)) {\n"
  "    case 'boolean':\n"
  "      return AQL_TYPEWEIGHT_BOOL;\n"
  "    case 'number':\n"
  "      if (isNaN(lhs) || !isFinite(lhs)) {\n"
  "        // not a number => undefined\n"
  "        return AQL_TYPEWEIGHT_UNDEFINED; \n"
  "      }\n"
  "      return AQL_TYPEWEIGHT_NUMBER;\n"
  "    case 'string':\n"
  "      return AQL_TYPEWEIGHT_STRING;\n"
  "    case 'object':\n"
  "      return AQL_TYPEWEIGHT_OBJECT;\n"
  "    default:\n"
  "      return AQL_TYPEWEIGHT_UNDEFINED;\n"
  "  }\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief get the keys of an array or object in a comparable way\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_KEYS (lhs) {\n"
  "  var keys = [];\n"
  "  \n"
  "  if (lhs instanceof Array) {\n"
  "    var i = 0;\n"
  "    for (var k in lhs) {\n"
  "      if (lhs.hasOwnProperty(k)) {\n"
  "        keys.push(i++);\n"
  "      }\n"
  "    }\n"
  "  }\n"
  "  else {\n"
  "    for (var k in lhs) {\n"
  "      if (lhs.hasOwnProperty(k)) {\n"
  "        keys.push(k);\n"
  "      }\n"
  "    }\n"
  "\n"
  "    // object keys need to be sorted by names\n"
  "    keys.sort();\n"
  "  }\n"
  "\n"
  "  return keys;\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief perform logical and\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_LOGICAL_AND (lhs, rhs) {\n"
  "  // lhs && rhs: both operands must be bools, returns a bool or undefined\n"
  "  if (AQL_TYPEWEIGHT(lhs) !== AQL_TYPEWEIGHT_BOOL ||\n"
  "      AQL_TYPEWEIGHT(rhs) !== AQL_TYPEWEIGHT_BOOL) {\n"
  "    return undefined; \n"
  "  }\n"
  "\n"
  "  return (lhs && rhs);\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief perform logical or\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_LOGICAL_OR (lhs, rhs) {\n"
  "  // lhs || rhs: both operands must be bools, returns a bool or undefined\n"
  "  if (AQL_TYPEWEIGHT(lhs) !== AQL_TYPEWEIGHT_BOOL ||\n"
  "      AQL_TYPEWEIGHT(rhs) !== AQL_TYPEWEIGHT_BOOL) {\n"
  "    return undefined; \n"
  "  }\n"
  "\n"
  "  return (lhs || rhs);\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief perform logical negation\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_LOGICAL_NOT (lhs) {\n"
  "  // !lhs: operand must be bool, returns a bool or undefined\n"
  "  if (AQL_TYPEWEIGHT(lhs) !== AQL_TYPEWEIGHT_BOOL) {\n"
  "    return undefined;\n"
  "  }\n"
  "\n"
  "  return !lhs;\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief perform equality check \n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_RELATIONAL_EQUAL (lhs, rhs) {\n"
  "  // determines if two values are the same, returns a bool or undefined\n"
  "  var leftWeight = AQL_TYPEWEIGHT(lhs);\n"
  "  var rightWeight = AQL_TYPEWEIGHT(rhs);\n"
  "\n"
  "  if (leftWeight === AQL_TYPEWEIGHT_UNDEFINED ||\n"
  "      rightWeight === AQL_TYPEWEIGHT_UNDEFINED) {\n"
  "    return undefined;\n"
  "  }\n"
  "\n"
  "  if (leftWeight != rightWeight) {\n"
  "    return false;\n"
  "  }\n"
  "\n"
  "  // lhs and rhs have the same type\n"
  "\n"
  "  if (leftWeight >= AQL_TYPEWEIGHT_ARRAY) {\n"
  "    // arrays and objects\n"
  "    var l = AQL_KEYS(lhs);\n"
  "    var r = AQL_KEYS(rhs);\n"
  "    var numLeft = l.length;\n"
  "    var numRight = r.length;\n"
  "\n"
  "    if (numLeft !== numRight) {\n"
  "      return false;\n"
  "    }\n"
  "\n"
  "    for (var i = 0; i < numLeft; ++i) {\n"
  "      var key = l[i];\n"
  "      if (key !== r[i]) {\n"
  "        // keys must be identical\n"
  "        return false;\n"
  "      }\n"
  "\n"
  "      var result = AQL_RELATIONAL_EQUAL(lhs[key], rhs[key]);\n"
  "      if (result === undefined || result === false) {\n"
  "        return result;\n"
  "      }\n"
  "    }\n"
  "    return true;\n"
  "  }\n"
  "\n"
  "  // primitive type\n"
  "  return (lhs === rhs);\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief perform unequality check \n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_RELATIONAL_UNEQUAL (lhs, rhs) {\n"
  "  // determines if two values are not the same, returns a bool or undefined\n"
  "  var leftWeight = AQL_TYPEWEIGHT(lhs);\n"
  "  var rightWeight = AQL_TYPEWEIGHT(rhs);\n"
  "  \n"
  "  if (leftWeight === AQL_TYPEWEIGHT_UNDEFINED ||\n"
  "      rightWeight === AQL_TYPEWEIGHT_UNDEFINED) {\n"
  "    return undefined;\n"
  "  }\n"
  "\n"
  "  if (leftWeight != rightWeight) {\n"
  "    return true;\n"
  "  }\n"
  "\n"
  "  // lhs and rhs have the same type\n"
  "\n"
  "  if (leftWeight >= AQL_TYPEWEIGHT_ARRAY) {\n"
  "    // arrays and objects\n"
  "    var l = AQL_KEYS(lhs);\n"
  "    var r = AQL_KEYS(rhs);\n"
  "    var numLeft = l.length;\n"
  "    var numRight = r.length;\n"
  "\n"
  "    if (numLeft !== numRight) {\n"
  "      return true;\n"
  "    }\n"
  "\n"
  "    for (var i = 0; i < numLeft; ++i) {\n"
  "      var key = l[i];\n"
  "      if (key !== r[i]) {\n"
  "        // keys differ => unequality\n"
  "        return true;\n"
  "      }\n"
  "\n"
  "      var result = AQL_RELATIONAL_UNEQUAL(lhs[key], rhs[key]);\n"
  "      if (result === undefined || result === true) {\n"
  "        return result;\n"
  "      }\n"
  "    }\n"
  "\n"
  "    return false;\n"
  "  }\n"
  "\n"
  "  // primitive type\n"
  "  return (lhs !== rhs);\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief perform greater than check (inner function)\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_RELATIONAL_GREATER_REC (lhs, rhs) {\n"
  "  // determines if lhs is greater than rhs, returns a bool or undefined\n"
  "  var leftWeight = AQL_TYPEWEIGHT(lhs);\n"
  "  var rightWeight = AQL_TYPEWEIGHT(rhs);\n"
  "  \n"
  "  if (leftWeight === AQL_TYPEWEIGHT_UNDEFINED ||\n"
  "      rightWeight === AQL_TYPEWEIGHT_UNDEFINED) {\n"
  "    return undefined;\n"
  "  }\n"
  "\n"
  "  if (leftWeight > rightWeight) {\n"
  "    return true;\n"
  "  }\n"
  "  if (leftWeight < rightWeight) {\n"
  "    return false;\n"
  "  }\n"
  "\n"
  "  // lhs and rhs have the same type\n"
  "\n"
  "  if (leftWeight >= AQL_TYPEWEIGHT_ARRAY) {\n"
  "    // arrays and objects\n"
  "    var l = AQL_KEYS(lhs);\n"
  "    var r = AQL_KEYS(rhs);\n"
  "    var numLeft = l.length;\n"
  "    var numRight = r.length;\n"
  "\n"
  "    for (var i = 0; i < numLeft; ++i) {\n"
  "      if (i >= numRight) {\n"
  "        // right operand does not have any more keys\n"
  "        return true;\n"
  "      }\n"
  "      var key = l[i];\n"
  "      if (key < r[i]) {\n"
  "        // left key is less than right key\n"
  "        return true;\n"
  "      } \n"
  "      else if (key > r[i]) {\n"
  "        // left key is bigger than right key\n"
  "        return false;\n"
  "      }\n"
  "\n"
  "      var result = AQL_RELATIONAL_GREATER_REC(lhs[key], rhs[key]);\n"
  "      if (result !== null) {\n"
  "        return result;\n"
  "      }\n"
  "    }\n"
  "    \n"
  "    if (numRight > numLeft) {\n"
  "      return false;\n"
  "    }\n"
  "\n"
  "    return null;\n"
  "  }\n"
  "\n"
  "  // primitive type\n"
  "  if (lhs === rhs) {\n"
  "    return null;\n"
  "  }\n"
  "  return (lhs > rhs);\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief perform greater than check \n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_RELATIONAL_GREATER (lhs, rhs) {\n"
  "  var result = AQL_RELATIONAL_GREATER_REC(lhs, rhs);\n"
  "\n"
  "  if (result === null) {\n"
  "    result = false;\n"
  "  }\n"
  "\n"
  "  return result;\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief perform greater equal check (inner function)\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_RELATIONAL_GREATEREQUAL_REC (lhs, rhs) {\n"
  "  // determines if lhs is greater or equal to rhs, returns a bool or undefined\n"
  "  var leftWeight = AQL_TYPEWEIGHT(lhs);\n"
  "  var rightWeight = AQL_TYPEWEIGHT(rhs);\n"
  "  \n"
  "  if (leftWeight === AQL_TYPEWEIGHT_UNDEFINED ||\n"
  "      rightWeight === AQL_TYPEWEIGHT_UNDEFINED) {\n"
  "    return undefined;\n"
  "  }\n"
  "\n"
  "  if (leftWeight > rightWeight) {\n"
  "    return true;\n"
  "  }\n"
  "  if (leftWeight < rightWeight) {\n"
  "    return false;\n"
  "  }\n"
  "\n"
  "  // lhs and rhs have the same type\n"
  "\n"
  "  if (leftWeight >= AQL_TYPEWEIGHT_ARRAY) {\n"
  "    // arrays and objects\n"
  "    var l = AQL_KEYS(lhs);\n"
  "    var r = AQL_KEYS(rhs);\n"
  "    var numLeft = l.length;\n"
  "    var numRight = r.length;\n"
  "\n"
  "    for (var i = 0; i < numLeft; ++i) {\n"
  "      if (i >= numRight) {\n"
  "        return true;\n"
  "      }\n"
  "      var key = l[i];\n"
  "      if (key < r[i]) {\n"
  "        // left key is less than right key\n"
  "        return true;\n"
  "      } \n"
  "      else if (key > r[i]) {\n"
  "        // left key is bigger than right key\n"
  "        return false;\n"
  "      }\n"
  "\n"
  "      var result = AQL_RELATIONAL_GREATEREQUAL_REC(lhs[key], rhs[key]);\n"
  "      if (result !== null) {\n"
  "        return result;\n"
  "      }\n"
  "    }\n"
  "    \n"
  "    if (numRight > numLeft) {\n"
  "      return false;\n"
  "    }\n"
  "\n"
  "    return null;\n"
  "  }\n"
  "\n"
  "  // primitive type\n"
  "  if (lhs === rhs) {\n"
  "    return null;\n"
  "  }\n"
  "  return (lhs >= rhs);\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief perform greater equal check \n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_RELATIONAL_GREATEREQUAL (lhs, rhs) {\n"
  "  var result = AQL_RELATIONAL_GREATEREQUAL_REC(lhs, rhs);\n"
  "\n"
  "  if (result === null) {\n"
  "    result = true;\n"
  "  }\n"
  "\n"
  "  return result;\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief perform less than check (inner function)\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_RELATIONAL_LESS_REC (lhs, rhs) {\n"
  "  // determines if lhs is less than rhs, returns a bool or undefined\n"
  "  var leftWeight = AQL_TYPEWEIGHT(lhs);\n"
  "  var rightWeight = AQL_TYPEWEIGHT(rhs);\n"
  "  \n"
  "  if (leftWeight === AQL_TYPEWEIGHT_UNDEFINED ||\n"
  "      rightWeight === AQL_TYPEWEIGHT_UNDEFINED) {\n"
  "    return undefined;\n"
  "  }\n"
  "\n"
  "  if (leftWeight < rightWeight) {\n"
  "    return true;\n"
  "  }\n"
  "  if (leftWeight > rightWeight) {\n"
  "    return false;\n"
  "  }\n"
  "\n"
  "  // lhs and rhs have the same type\n"
  "\n"
  "  if (leftWeight >= AQL_TYPEWEIGHT_ARRAY) {\n"
  "    // arrays and objects\n"
  "    var l = AQL_KEYS(lhs);\n"
  "    var r = AQL_KEYS(rhs);\n"
  "    var numLeft = l.length;\n"
  "    var numRight = r.length;\n"
  "\n"
  "    for (var i = 0; i < numRight; ++i) {\n"
  "      if (i >= numLeft) {\n"
  "        // left operand does not have any more keys\n"
  "        return true;\n"
  "      }\n"
  "      var key = l[i];\n"
  "      if (key < r[i]) {\n"
  "        // left key is less than right key\n"
  "        return false;\n"
  "      } \n"
  "      else if (key > r[i]) {\n"
  "        // left key is bigger than right key (\"b\", \"a\") {\"b\" : 1}, {\"a\" : 1}\n"
  "        return true;\n"
  "      }\n"
  "      // keys are equal\n"
  "      var result = AQL_RELATIONAL_LESS_REC(lhs[key], rhs[key]);\n"
  "      if (result !== null) {\n"
  "        return result;\n"
  "      }\n"
  "    }\n"
  "    \n"
  "    if (numLeft > numRight) {\n"
  "      return false;\n"
  "    }\n"
  "\n"
  "    return null;\n"
  "  }\n"
  "\n"
  "  // primitive type\n"
  "  if (lhs === rhs) {\n"
  "    return null;\n"
  "  }\n"
  "  return (lhs < rhs);\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief perform less than check \n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_RELATIONAL_LESS (lhs, rhs) {\n"
  "  var result = AQL_RELATIONAL_LESS_REC(lhs, rhs);\n"
  "\n"
  "  if (result === null) {\n"
  "    result = false;\n"
  "  }\n"
  "\n"
  "  return result;\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief perform less equal check (inner function)\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_RELATIONAL_LESSEQUAL_REC (lhs, rhs) {\n"
  "  // determines if lhs is less or equal to rhs, returns a bool or undefined\n"
  "  var leftWeight = AQL_TYPEWEIGHT(lhs);\n"
  "  var rightWeight = AQL_TYPEWEIGHT(rhs);\n"
  "  \n"
  "  if (leftWeight === AQL_TYPEWEIGHT_UNDEFINED ||\n"
  "      rightWeight === AQL_TYPEWEIGHT_UNDEFINED) {\n"
  "    return undefined;\n"
  "  }\n"
  "\n"
  "  if (leftWeight < rightWeight) {\n"
  "    return true;\n"
  "  }\n"
  "  if (leftWeight > rightWeight) {\n"
  "    return false;\n"
  "  }\n"
  "\n"
  "  // lhs and rhs have the same type\n"
  "\n"
  "  if (leftWeight >= AQL_TYPEWEIGHT_ARRAY) {\n"
  "    // arrays and objects\n"
  "    var l = AQL_KEYS(lhs);\n"
  "    var r = AQL_KEYS(rhs);\n"
  "    var numLeft = l.length;\n"
  "    var numRight = r.length;\n"
  "\n"
  "    for (var i = 0; i < numRight; ++i) {\n"
  "      if (i >= numLeft) {\n"
  "        return true;\n"
  "      }\n"
  "      var key = l[i];\n"
  "      if (key < r[i]) {\n"
  "        // left key is less than right key\n"
  "        return false;\n"
  "      } \n"
  "      else if (key > r[i]) {\n"
  "        // left key is bigger than right key\n"
  "        return true;\n"
  "      }\n"
  "      var result = AQL_RELATIONAL_LESSEQUAL_REC(lhs[key], rhs[key]);\n"
  "      if (result !== null) {\n"
  "        return result;\n"
  "      }\n"
  "    }\n"
  "\n"
  "    if (numLeft > numRight) {\n"
  "      return false;\n"
  "    }\n"
  "\n"
  "    return null;\n"
  "  }\n"
  "  \n"
  "  if (lhs === rhs) {\n"
  "    return null;\n"
  "  }\n"
  "\n"
  "  // primitive type\n"
  "  return (lhs <= rhs);\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief perform less than check \n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_RELATIONAL_LESSEQUAL (lhs, rhs) {\n"
  "  var result = AQL_RELATIONAL_LESSEQUAL_REC(lhs, rhs);\n"
  "\n"
  "  if (result === null) {\n"
  "    result = true;\n"
  "  }\n"
  "\n"
  "  return result;\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief perform in list check \n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_RELATIONAL_IN (lhs, rhs) {\n"
  "  // lhs IN rhs: both operands must have the same type, returns a bool or undefined\n"
  "  var leftWeight = AQL_TYPEWEIGHT(lhs);\n"
  "  var rightWeight = AQL_TYPEWEIGHT(rhs);\n"
  "  \n"
  "  if (leftWeight === AQL_TYPEWEIGHT_UNDEFINED ||\n"
  "      rightWeight !== AQL_TYPEWEIGHT_ARRAY) {\n"
  "    return undefined;\n"
  "  }\n"
  "  \n"
  "  var r = AQL_KEYS(rhs);\n"
  "  var numRight = r.length;\n"
  "\n"
  "  for (var i = 0; i < numRight; ++i) {\n"
  "    var key = r[i];\n"
  "    if (AQL_RELATIONAL_EQUAL(lhs, rhs[key])) {\n"
  "      return true;\n"
  "    }\n"
  "  }\n"
  "\n"
  "  return false;\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief perform unary plus operation\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_UNARY_PLUS (lhs) {\n"
  "  // +lhs: operand must be numeric, returns a number or undefined\n"
  "  if (AQL_TYPEWEIGHT(lhs) !== AQL_TYPEWEIGHT_NUMBER) {\n"
  "    return undefined;\n"
  "  }\n"
  "\n"
  "  return AQL_NUMERIC_VALUE(lhs);\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief perform unary minus operation\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_UNARY_MINUS (lhs) {\n"
  "  // -lhs: operand must be numeric, returns a number or undefined\n"
  "  if (AQL_TYPEWEIGHT(lhs) !== AQL_TYPEWEIGHT_NUMBER) {\n"
  "    return undefined;\n"
  "  }\n"
  "\n"
  "  return AQL_NUMERIC_VALUE(-lhs);\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief perform artithmetic plus\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_ARITHMETIC_PLUS (lhs, rhs) { \n"
  "  // lhs + rhs: operands must be numeric, returns a number or undefined\n"
  "  if (AQL_TYPEWEIGHT(lhs) !== AQL_TYPEWEIGHT_NUMBER ||\n"
  "      AQL_TYPEWEIGHT(rhs) !== AQL_TYPEWEIGHT_NUMBER) {\n"
  "    return undefined;\n"
  "  }\n"
  "\n"
  "  return AQL_NUMERIC_VALUE(lhs + rhs);\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief perform artithmetic minus\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_ARITHMETIC_MINUS (lhs, rhs) {\n"
  "  // lhs - rhs: operands must be numeric, returns a number or undefined\n"
  "  if (AQL_TYPEWEIGHT(lhs) !== AQL_TYPEWEIGHT_NUMBER ||\n"
  "      AQL_TYPEWEIGHT(rhs) !== AQL_TYPEWEIGHT_NUMBER) {\n"
  "    return undefined;\n"
  "  }\n"
  "\n"
  "  return AQL_NUMERIC_VALUE(lhs - rhs);\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief perform artithmetic multiplication\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_ARITHMETIC_TIMES (lhs, rhs) {\n"
  "  // lhs * rhs: operands must be numeric, returns a number or undefined\n"
  "  if (AQL_TYPEWEIGHT(lhs) !== AQL_TYPEWEIGHT_NUMBER ||\n"
  "      AQL_TYPEWEIGHT(rhs) !== AQL_TYPEWEIGHT_NUMBER) {\n"
  "    return undefined;\n"
  "  }\n"
  "\n"
  "  return AQL_NUMERIC_VALUE(lhs * rhs);\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief perform artithmetic division\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_ARITHMETIC_DIVIDE (lhs, rhs) {\n"
  "  // lhs / rhs: operands must be numeric, returns a number or undefined\n"
  "  if (AQL_TYPEWEIGHT(lhs) !== AQL_TYPEWEIGHT_NUMBER ||\n"
  "      AQL_TYPEWEIGHT(rhs) !== AQL_TYPEWEIGHT_NUMBER ||\n"
  "      rhs == 0) {\n"
  "    return undefined;\n"
  "  }\n"
  "\n"
  "  return AQL_NUMERIC_VALUE(lhs / rhs);\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief perform artithmetic modulus\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_ARITHMETIC_MODULUS (lhs, rhs) {\n"
  "  // lhs % rhs: operands must be numeric, returns a number or undefined\n"
  "  if (AQL_TYPEWEIGHT(lhs) !== AQL_TYPEWEIGHT_NUMBER ||\n"
  "      AQL_TYPEWEIGHT(rhs) !== AQL_TYPEWEIGHT_NUMBER ||\n"
  "      rhs == 0) {\n"
  "    return undefined;\n"
  "  }\n"
  "\n"
  "  return AQL_NUMERIC_VALUE(lhs % rhs);\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief perform string concatenation\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_STRING_CONCAT (lhs, rhs) {\n"
  "  // string concatenation of lhs and rhs: operands must be strings, returns a string or undefined\n"
  "  if (AQL_TYPEWEIGHT(lhs) !== AQL_TYPEWEIGHT_STRING ||\n"
  "      AQL_TYPEWEIGHT(rhs) !== AQL_TYPEWEIGHT_STRING) {\n"
  "    return undefined;\n"
  "  }\n"
  "\n"
  "  return (lhs + rhs);\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief cast to a number\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_CAST_NUMBER (lhs) {\n"
  "  // (number) lhs: operand can have any type, returns a number\n"
  "  switch (AQL_TYPEWEIGHT(lhs)) {\n"
  "    case AQL_TYPEWEIGHT_UNDEFINED:\n"
  "    case AQL_TYPEWEIGHT_NULL:\n"
  "    case AQL_TYPEWEIGHT_ARRAY:\n"
  "    case AQL_TYPEWEIGHT_OBJECT:\n"
  "      return 0.0;\n"
  "    case AQL_TYPEWEIGHT_BOOL:\n"
  "      return (lhs ? 1 : 0);\n"
  "    case AQL_TYPEWEIGHT_NUMBER:\n"
  "      return lhs;\n"
  "    case AQL_TYPEWEIGHT_STRING:\n"
  "      var result = parseFloat(lhs);\n"
  "      return ((AQL_TYPEWEIGHT(result) === AQL_TYPEWEIGHT_NUMBER) ? result : 0);\n"
  "  }\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief cast to a string\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_CAST_STRING (lhs) {\n"
  "  // (string) lhs: operand can have any type, returns a string\n"
  "  switch (AQL_TYPEWEIGHT(lhs)) {\n"
  "    case AQL_TYPEWEIGHT_STRING:\n"
  "      return lhs;\n"
  "    case AQL_TYPEWEIGHT_UNDEFINED:\n"
  "      return 'undefined';\n"
  "    case AQL_TYPEWEIGHT_NULL:\n"
  "      return 'null';\n"
  "    case AQL_TYPEWEIGHT_BOOL:\n"
  "      return (lhs ? 'true' : 'false');\n"
  "    case AQL_TYPEWEIGHT_NUMBER:\n"
  "    case AQL_TYPEWEIGHT_ARRAY:\n"
  "    case AQL_TYPEWEIGHT_OBJECT:\n"
  "      return lhs.toString();\n"
  "  }\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief cast to a bool\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_CAST_BOOL (lhs) {\n"
  "  // (bool) lhs: operand can have any type, returns a bool\n"
  "  switch (AQL_TYPEWEIGHT(lhs)) {\n"
  "    case AQL_TYPEWEIGHT_UNDEFINED:\n"
  "    case AQL_TYPEWEIGHT_NULL:\n"
  "      return false;\n"
  "    case AQL_TYPEWEIGHT_BOOL:\n"
  "      return lhs;\n"
  "    case AQL_TYPEWEIGHT_NUMBER:\n"
  "      return (lhs != 0);\n"
  "    case AQL_TYPEWEIGHT_STRING: \n"
  "      return (lhs != '');\n"
  "    case AQL_TYPEWEIGHT_ARRAY:\n"
  "      return (lhs.length > 0);\n"
  "    case AQL_TYPEWEIGHT_OBJECT:\n"
  "      return (AQL_KEYS(lhs).length > 0);\n"
  "  }\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief cast to null\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_CAST_NULL (lhs) {\n"
  "  // (null) lhs: operand can have any type, always returns null\n"
  "  return null;\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief cast to undefined\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_CAST_UNDEFINED (lhs) {\n"
  "  // (null) lhs: operand can have any type, always returns undefined\n"
  "  return undefined;\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief test if value is of type string\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_IS_STRING (lhs) {\n"
  "  return (AQL_TYPEWEIGHT(lhs) === AQL_TYPEWEIGHT_STRING);\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief test if value is of type number\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_IS_NUMBER (lhs) {\n"
  "  return (AQL_TYPEWEIGHT(lhs) === AQL_TYPEWEIGHT_NUMBER);\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief test if value is of type bool\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_IS_BOOL (lhs) {\n"
  "  return (AQL_TYPEWEIGHT(lhs) === AQL_TYPEWEIGHT_BOOL);\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief test if value is of type null\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_IS_NULL (lhs) {\n"
  "  return (AQL_TYPEWEIGHT(lhs) === AQL_TYPEWEIGHT_NULL);\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief test if value is of type undefined\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_IS_UNDEFINED (lhs) {\n"
  "  return (AQL_TYPEWEIGHT(lhs) === AQL_TYPEWEIGHT_UNDEFINED);\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief test if value is of type array\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_IS_ARRAY (lhs) {\n"
  "  return (AQL_TYPEWEIGHT(lhs) === AQL_TYPEWEIGHT_ARRAY);\n"
  "}\n"
  "\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "/// @brief test if value is of type object\n"
  "////////////////////////////////////////////////////////////////////////////////\n"
  "\n"
  "function AQL_IS_OBJECT (lhs) {\n"
  "  return (AQL_TYPEWEIGHT(lhs) === AQL_TYPEWEIGHT_OBJECT);\n"
  "}\n"
  "\n"
;
