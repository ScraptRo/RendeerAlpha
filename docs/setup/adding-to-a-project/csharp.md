# A C# project

A .NET project has a build of its own, so the tool is called from the `.csproj` and
`dotnet run` does everything. The binding is two source files compiled in, and the
engine is a DLL copied beside the executable.

## The tree

```
MyApp/
├── MyApp.csproj
├── Program.cs
├── state.cs             generated -- what `rda build` writes from res/state.ts
└── res/                 exactly as the others
```

## MyApp.csproj

```xml
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net8.0</TargetFramework>
    <Nullable>enable</Nullable>
    <RdaEngine>../RendeerAlpha</RdaEngine>
  </PropertyGroup>

  <ItemGroup>
    <Compile Include="$(RdaEngine)/bindings/csharp/*.cs" />
    <Compile Include="state.cs" />
    <!-- The engine beside the executable, where .NET looks first, and its font
         beside the engine, where the engine looks. -->
    <None Include="$(RdaEngine)/bin/rendeer_c.dll" Link="rendeer_c.dll"
          CopyToOutputDirectory="PreserveNewest" />
    <None Include="$(RdaEngine)/bin/res/**" Link="res/%(RecursiveDir)%(Filename)%(Extension)"
          CopyToOutputDirectory="PreserveNewest" />
  </ItemGroup>

  <!-- The interface, before the C# is compiled: state.cs comes out of this. -->
  <Target Name="RdaBuild" BeforeTargets="BeforeBuild">
    <Exec Command="&quot;$(RdaEngine)/bin/rda&quot; build &quot;$(MSBuildProjectDirectory)&quot; --csharp" />
  </Target>
</Project>
```

On Linux the library is `librendeer_c.so`; change the one `Include`.

The binding is two source files compiled in, rather than a package reference — a NuGet
feed is a lot of machinery to avoid copying two files, and compiling them means what you
build is what is in the checkout. `Rda.csproj` in `bindings/csharp/` is there if you
would rather reference a library.

## One command

```bash
dotnet run
```

`dotnet run` builds, which runs the tool over `res/` first — every theme to its `.rdth`,
every layout to its `.rdab`, `res/layouts/rda.d.ts`, and `state.cs` — then compiles the
application and starts it from the project directory, which is where `res/` resolves
from. The engine's DLL and font were copied beside the executable by the two `None`
items, and the engine finds the font beside itself.

`RDA_ENGINE=<checkout>/bin` in the environment makes the binding load the DLL from there
instead, for a program that would rather not copy it.

[The first interface in C#](../first-interface/csharp.md) is the `Program.cs` that goes
with this.

---

Back to [adding to a project](README.md) · [setup](../README.md) · [all documentation](../../README.md)
