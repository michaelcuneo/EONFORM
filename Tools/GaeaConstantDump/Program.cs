using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;

if (args.Length < 1)
{
    Console.Error.WriteLine("Usage:");
    Console.Error.WriteLine("  GaeaConstantDump <Gaea.Nodes.dll> [indices...]");
    Console.Error.WriteLine("  GaeaConstantDump <Gaea.Nodes.dll> --scan <terms...>");
    Environment.Exit(2);
}

var dll = Path.GetFullPath(args[0]);
if (!File.Exists(dll))
{
    Console.Error.WriteLine($"DLL not found: {dll}");
    Environment.Exit(2);
}

var asm = Assembly.LoadFrom(dll);

if (args.Length > 1 && args[1].Equals("--scan", StringComparison.OrdinalIgnoreCase))
{
    var terms = args.Skip(2).Where(x => !string.IsNullOrWhiteSpace(x)).ToArray();
    if (terms.Length == 0)
        terms = new[] { "Ridge", "Voronoi", "RawNoise" };

    DumpMatchingMethods(asm, terms);
    return;
}

var indices = args.Length > 1
    ? args.Skip(1).Select(int.Parse).ToArray()
    : new[] { 1, 95 };

// This is the exact working access path used in the previous dumper.
var constantsType = asm.GetType("\uE0003", throwOnError: true)!;
RuntimeHelpers.RunClassConstructor(constantsType.TypeHandle);

var holderType = constantsType
    .GetNestedTypes(BindingFlags.NonPublic | BindingFlags.Public)
    .First(t =>
    {
        var fields = t.GetFields(
            BindingFlags.Instance |
            BindingFlags.Static |
            BindingFlags.NonPublic |
            BindingFlags.Public);

        return fields.Any(f => f.FieldType == typeof(int[])) &&
               fields.Any(f => f.FieldType == typeof(float[]));
    });

RuntimeHelpers.RunClassConstructor(holderType.TypeHandle);

var allFields = holderType.GetFields(
    BindingFlags.Instance |
    BindingFlags.Static |
    BindingFlags.NonPublic |
    BindingFlags.Public);

object? holder = allFields
    .Where(f => f.IsStatic && f.FieldType == holderType)
    .Select(f => f.GetValue(null))
    .FirstOrDefault(v => v != null);

if (holder == null)
    throw new Exception("Could not locate constants holder instance.");

var floatField = holderType
    .GetFields(BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public)
    .First(f => f.FieldType == typeof(float[]));

var floats = (float[])floatField.GetValue(holder)!;

foreach (var index in indices)
{
    if (index < 0 || index >= floats.Length)
    {
        Console.WriteLine($"e002({index}) = <out of range; len={floats.Length}>");
        continue;
    }

    Console.WriteLine($"e002({index}) = {floats[index]:R}");
}

static void DumpMatchingMethods(Assembly asm, string[] terms)
{
    var flags = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
    var types = GetLoadableTypes(asm).OrderBy(t => t.FullName, StringComparer.Ordinal).ToArray();
    var matched = 0;

    foreach (var type in types)
    {
        MethodBase[] methods;
        try
        {
            methods = type.GetMethods(flags).Cast<MethodBase>()
                .Concat(type.GetConstructors(flags))
                .OrderBy(m => m.MetadataToken)
                .ToArray();
        }
        catch
        {
            continue;
        }

        var typeName = type.FullName ?? type.Name;
        var typeMatches = terms.Any(term => typeName.Contains(term, StringComparison.OrdinalIgnoreCase));
        var matchingMethods = typeMatches
            ? methods
            : methods.Where(m => terms.Any(term => m.Name.Contains(term, StringComparison.OrdinalIgnoreCase))).ToArray();

        if (matchingMethods.Length == 0)
            continue;

        matched++;
        Console.WriteLine();
        Console.WriteLine(new string('=', 100));
        Console.WriteLine($"TYPE {typeName}");
        Console.WriteLine(new string('=', 100));

        foreach (var method in matchingMethods)
        {
            DumpMethod(method);
        }
    }

    if (matched == 0)
    {
        Console.WriteLine($"No types or methods matched: {string.Join(", ", terms)}");
        Console.WriteLine("Nearby type names containing terrain/noise keywords:");
        foreach (var type in types.Where(t =>
                     (t.FullName ?? t.Name).Contains("terrain", StringComparison.OrdinalIgnoreCase) ||
                     (t.FullName ?? t.Name).Contains("noise", StringComparison.OrdinalIgnoreCase) ||
                     (t.FullName ?? t.Name).Contains("landscape", StringComparison.OrdinalIgnoreCase)).Take(200))
        {
            Console.WriteLine($"  {type.FullName ?? type.Name}");
        }
    }
}

static Type[] GetLoadableTypes(Assembly asm)
{
    try
    {
        return asm.GetTypes();
    }
    catch (ReflectionTypeLoadException ex)
    {
        foreach (var loaderException in ex.LoaderExceptions.Where(e => e != null))
            Console.Error.WriteLine($"Loader warning: {loaderException!.Message}");
        return ex.Types.Where(t => t != null).Cast<Type>().ToArray();
    }
}

static void DumpMethod(MethodBase method)
{
    Console.WriteLine();
    Console.WriteLine($"METHOD 0x{method.MetadataToken:X8} {FormatMethod(method)}");

    MethodBody? body;
    try
    {
        body = method.GetMethodBody();
    }
    catch (Exception ex)
    {
        Console.WriteLine($"  <body unavailable: {ex.Message}>");
        return;
    }

    var il = body?.GetILAsByteArray();
    if (il == null || il.Length == 0)
    {
        Console.WriteLine("  <no IL body>");
        return;
    }

    var module = method.Module;
    var typeArgs = method.DeclaringType?.IsGenericType == true ? method.DeclaringType.GetGenericArguments() : Type.EmptyTypes;
    var methodArgs = method.IsGenericMethod ? method.GetGenericArguments() : Type.EmptyTypes;
    var position = 0;

    while (position < il.Length)
    {
        var offset = position;
        var op = ReadOpCode(il, ref position);
        var operand = ReadOperand(il, ref position, op, module, typeArgs, methodArgs);
        Console.WriteLine(operand.Length == 0
            ? $"  IL_{offset:X4}: {op.Name}"
            : $"  IL_{offset:X4}: {op.Name,-14} {operand}");
    }
}

static string FormatMethod(MethodBase method)
{
    var parameters = string.Join(", ", method.GetParameters().Select(p => $"{FriendlyTypeName(p.ParameterType)} {p.Name}"));
    var returnType = method is MethodInfo mi ? FriendlyTypeName(mi.ReturnType) : "void";
    return $"{returnType} {method.DeclaringType?.FullName ?? "<unknown>"}.{method.Name}({parameters})";
}

static string FriendlyTypeName(Type type)
{
    if (type.IsByRef)
        return FriendlyTypeName(type.GetElementType()!) + "&";
    if (type.IsArray)
        return FriendlyTypeName(type.GetElementType()!) + "[]";
    if (!type.IsGenericType)
        return type.FullName ?? type.Name;

    var name = type.GetGenericTypeDefinition().FullName ?? type.Name;
    var tick = name.IndexOf('`');
    if (tick >= 0)
        name = name[..tick];
    return $"{name}<{string.Join(", ", type.GetGenericArguments().Select(FriendlyTypeName))}>";
}

static readonly Dictionary<ushort, OpCode> OneByteOpCodes = typeof(OpCodes)
    .GetFields(BindingFlags.Public | BindingFlags.Static)
    .Where(f => f.FieldType == typeof(OpCode))
    .Select(f => (OpCode)f.GetValue(null)!)
    .Where(op => op.Size == 1)
    .ToDictionary(op => unchecked((ushort)op.Value));

static readonly Dictionary<ushort, OpCode> TwoByteOpCodes = typeof(OpCodes)
    .GetFields(BindingFlags.Public | BindingFlags.Static)
    .Where(f => f.FieldType == typeof(OpCode))
    .Select(f => (OpCode)f.GetValue(null)!)
    .Where(op => op.Size == 2)
    .ToDictionary(op => unchecked((ushort)op.Value));

static OpCode ReadOpCode(byte[] il, ref int position)
{
    var first = il[position++];
    if (first != 0xFE)
    {
        if (OneByteOpCodes.TryGetValue(first, out var op))
            return op;
        throw new InvalidOperationException($"Unknown IL opcode 0x{first:X2}.");
    }

    var second = il[position++];
    var key = (ushort)(0xFE00 | second);
    if (TwoByteOpCodes.TryGetValue(key, out var twoByte))
        return twoByte;
    throw new InvalidOperationException($"Unknown IL opcode 0x{key:X4}.");
}

static string ReadOperand(byte[] il, ref int position, OpCode op, Module module, Type[] typeArgs, Type[] methodArgs)
{
    switch (op.OperandType)
    {
        case OperandType.InlineNone:
            return string.Empty;
        case OperandType.ShortInlineI:
            return ((sbyte)il[position++]).ToString();
        case OperandType.InlineI:
            return ReadInt32(il, ref position).ToString();
        case OperandType.InlineI8:
            return ReadInt64(il, ref position).ToString();
        case OperandType.ShortInlineR:
            return ReadSingle(il, ref position).ToString("R");
        case OperandType.InlineR:
            return ReadDouble(il, ref position).ToString("R");
        case OperandType.ShortInlineVar:
            return il[position++].ToString();
        case OperandType.InlineVar:
            return ReadUInt16(il, ref position).ToString();
        case OperandType.ShortInlineBrTarget:
        {
            var delta = (sbyte)il[position++];
            return $"IL_{position + delta:X4}";
        }
        case OperandType.InlineBrTarget:
        {
            var delta = ReadInt32(il, ref position);
            return $"IL_{position + delta:X4}";
        }
        case OperandType.InlineSwitch:
        {
            var count = ReadInt32(il, ref position);
            var basePosition = position + count * 4;
            var targets = new string[count];
            for (var i = 0; i < count; i++)
            {
                var delta = ReadInt32(il, ref position);
                targets[i] = $"IL_{basePosition + delta:X4}";
            }
            return string.Join(", ", targets);
        }
        case OperandType.InlineString:
        {
            var token = ReadInt32(il, ref position);
            try { return $"\"{module.ResolveString(token)}\""; }
            catch { return $"string(0x{token:X8})"; }
        }
        case OperandType.InlineField:
        case OperandType.InlineMethod:
        case OperandType.InlineType:
        case OperandType.InlineTok:
        {
            var token = ReadInt32(il, ref position);
            try
            {
                var member = module.ResolveMember(token, typeArgs, methodArgs);
                return member == null ? $"token(0x{token:X8})" : $"{member.DeclaringType?.FullName}.{member}";
            }
            catch
            {
                return $"token(0x{token:X8})";
            }
        }
        case OperandType.InlineSig:
        {
            var token = ReadInt32(il, ref position);
            return $"sig(0x{token:X8})";
        }
        default:
            throw new NotSupportedException($"Unsupported IL operand type {op.OperandType} for {op.Name}.");
    }
}

static int ReadInt32(byte[] data, ref int position)
{
    var value = BitConverter.ToInt32(data, position);
    position += 4;
    return value;
}

static long ReadInt64(byte[] data, ref int position)
{
    var value = BitConverter.ToInt64(data, position);
    position += 8;
    return value;
}

static ushort ReadUInt16(byte[] data, ref int position)
{
    var value = BitConverter.ToUInt16(data, position);
    position += 2;
    return value;
}

static float ReadSingle(byte[] data, ref int position)
{
    var value = BitConverter.ToSingle(data, position);
    position += 4;
    return value;
}

static double ReadDouble(byte[] data, ref int position)
{
    var value = BitConverter.ToDouble(data, position);
    position += 8;
    return value;
}
