using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;

if (args.Length < 1)
{
    Console.Error.WriteLine("Usage: GaeaConstantDump <Gaea.Nodes.dll> [indices...]");
    Environment.Exit(2);
}

var dll = Path.GetFullPath(args[0]);
var indices = args.Length > 1
    ? args.Skip(1).Select(int.Parse).ToArray()
    : new[] { 1, 95 };

if (!File.Exists(dll))
{
    Console.Error.WriteLine($"DLL not found: {dll}");
    Environment.Exit(2);
}

var asm = Assembly.LoadFrom(dll);

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
