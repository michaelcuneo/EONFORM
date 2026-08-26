using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;

class Program
{
    static void Main()
    {
        var dll = @"C:\PATH\TO\Gaea.Nodes.dll";

        var asm = Assembly.LoadFrom(dll);

        // Type name: \ue0003
        var constantsType = asm.GetType("\uE0003", throwOnError: true)!;

        // Force the class / nested singleton to initialise.
        RuntimeHelpers.RunClassConstructor(constantsType.TypeHandle);

        // Find the nested class that owns:
        // int[], long[], float[], double[]
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

        // Force its static constructor too.
        RuntimeHelpers.RunClassConstructor(holderType.TypeHandle);

        var allFields = holderType.GetFields(
            BindingFlags.Instance |
            BindingFlags.Static |
            BindingFlags.NonPublic |
            BindingFlags.Public);

        // Find the singleton instance of that nested holder.
        object? holder = allFields
            .Where(f => f.IsStatic && f.FieldType == holderType)
            .Select(f => f.GetValue(null))
            .FirstOrDefault(v => v != null);

        if (holder == null)
            throw new Exception("Could not locate constants holder instance.");

        var instanceFields = holderType.GetFields(
            BindingFlags.Instance |
            BindingFlags.NonPublic |
            BindingFlags.Public);

        var ints = (int[])instanceFields
            .First(f => f.FieldType == typeof(int[]))
            .GetValue(holder)!;

        var longs = (long[])instanceFields
            .First(f => f.FieldType == typeof(long[]))
            .GetValue(holder)!;

        var floats = (float[])instanceFields
            .First(f => f.FieldType == typeof(float[]))
            .GetValue(holder)!;

        var doubles = (double[])instanceFields
            .First(f => f.FieldType == typeof(double[]))
            .GetValue(holder)!;

        using (var w = new StreamWriter("gaea_int_constants.txt"))
        {
            for (int i = 0; i < ints.Length; i++)
                w.WriteLine($"{i} = {ints[i]}");
        }

        using (var w = new StreamWriter("gaea_float_constants.txt"))
        {
            for (int i = 0; i < floats.Length; i++)
                w.WriteLine($"{i} = {floats[i]:R}");
        }

        using (var w = new StreamWriter("gaea_long_constants.txt"))
        {
            for (int i = 0; i < longs.Length; i++)
                w.WriteLine($"{i} = {longs[i]}");
        }

        using (var w = new StreamWriter("gaea_double_constants.txt"))
        {
            for (int i = 0; i < doubles.Length; i++)
                w.WriteLine($"{i} = {doubles[i]:R}");
        }

        Console.WriteLine($"Ints:    {ints.Length}");
        Console.WriteLine($"Longs:   {longs.Length}");
        Console.WriteLine($"Floats:  {floats.Length}");
        Console.WriteLine($"Doubles: {doubles.Length}");

        Console.WriteLine();
        Console.WriteLine("Important terrain constants:");

        foreach (var i in new[] { 0, 1, 2, 5, 69, 70, 71, 72, 74, 81, 82, 83, 88, 93, 95, 97, 102, 103, 104, 105, 106, 107, 113, 126, 127, 135, 142, 164, 168 })
        {
            if (i < floats.Length)
                Console.WriteLine($"ue002({i}) = {floats[i]:R}");
        }

        Console.WriteLine();
        Console.WriteLine("INTEGER CONSTANT METHODS:");

        int[] intIndices =
        {
    0, 1, 2, 3, 5, 8, 11, 13, 29, 35
};

        foreach (var type in asm.GetTypes())
        {
            foreach (var method in type.GetMethods(
                BindingFlags.Static |
                BindingFlags.Public |
                BindingFlags.NonPublic))
            {
                var p = method.GetParameters();

                if (method.ReturnType != typeof(int) ||
                    p.Length != 1 ||
                    p[0].ParameterType != typeof(int))
                    continue;

                try
                {
                    int v0 = (int)method.Invoke(null, new object[] { 0 })!;
                    int v1 = (int)method.Invoke(null, new object[] { 1 })!;

                    if (v0 == 0 && v1 == 1)
                    {
                        Console.WriteLine($"CANDIDATE: {type.FullName}.{method.Name}");

                        foreach (int index in intIndices)
                        {
                            int value =
                                (int)method.Invoke(null, new object[] { index })!;

                            Console.WriteLine(
                                $"e000({index}) = {value}");
                        }

                        Console.WriteLine();
                    }
                }
                catch
                {
                }
            }
        }
    }
}