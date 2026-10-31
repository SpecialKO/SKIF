<#
.SYNOPSIS
  Extracts the translatable text of SKIF into the gettext template resources\l10n\SKIF.pot.

.DESCRIPTION
  Scans the sources (src\ and include\, without the third-party code in imgui\ and MinHook\) for the
  localization macros of include\utility\l10n.h and writes every text they mark to the template, which
  translation tools (Weblate, Crowdin, Poedit) use to create and update the .po file of each language.

    _("text")             C_("context", "text")    W_("text")     CW_("context", "text")
    N_("text")            NC_("context", "text")    NL_("label")
    L_("label")           LI_(icon, "label")       CL_("context", "label")
    P_("one", "many", n)  F_("text", ...)          FP_("one", "many", n, ...)

  The text must be string literals (adjacent literals are joined, as by the compiler). For labels, only the part
  before "##" / "###" is extracted. A comment starting with "TRANSLATORS:" before the text (or on the same line)
  is passed on to translators. A "TRANSLATORS:" comment applies to the next text within three lines.

  Needs nothing but Windows PowerShell 5.1 (included in Windows) or PowerShell 7 (also on Linux and macOS).
  The template is only rewritten if the text changed (not just the date).

  With -Check, nothing is written: the script only checks that the template is up to date, and exits with 1 if it
  is not (or if a text could not be extracted). Source references (#: lines) and the date are ignored, so moving
  code around does not make the template out of date; adding, changing or removing a text (or its context, plural
  form or TRANSLATORS: comment) does. Used by the l10n workflow (.github\workflows\l10n.yml).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\l10n_extract.ps1

.EXAMPLE
  pwsh -File tools/l10n_extract.ps1 -Check
#>

[CmdletBinding()]
param (
  # Root of the repository (default: the parent folder of this script)
  [string] $Root   = (Split-Path -Parent $PSScriptRoot),
  # The template to write, relative to the root
  [string] $Output = 'resources\l10n\SKIF.pot',
  # Only check that the template is up to date (see above); write nothing
  [switch] $Check
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$Root     = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\', '/')
$utf8     = New-Object System.Text.UTF8Encoding($false)
$warnings = 0

# Folders and files that are not scanned (relative to the root, with forward slashes)
$excluded = @('src/imgui/', 'include/imgui/', 'src/MinHook/', 'include/MinHook', 'include/fonts/', 'include/utility/l10n.h')

# Keyword -> which arguments hold the context, the text, and the plural form (0-based; -1 = none), and whether it is a label
$keywords = @{
  '_'   = @{ Ctx = -1; Id = 0; Plural = -1; Label = $false }
  'C_'  = @{ Ctx =  0; Id = 1; Plural = -1; Label = $false }
  'L_'  = @{ Ctx = -1; Id = 0; Plural = -1; Label = $true  }
  'LI_' = @{ Ctx = -1; Id = 1; Plural = -1; Label = $true  }
  'CL_' = @{ Ctx =  0; Id = 1; Plural = -1; Label = $true  }
  'W_'  = @{ Ctx = -1; Id = 0; Plural = -1; Label = $false }
  'CW_' = @{ Ctx =  0; Id = 1; Plural = -1; Label = $false }
  'N_'  = @{ Ctx = -1; Id = 0; Plural = -1; Label = $false }
  'NC_' = @{ Ctx =  0; Id = 1; Plural = -1; Label = $false }
  'NL_' = @{ Ctx = -1; Id = 0; Plural = -1; Label = $true  }
  'P_'  = @{ Ctx = -1; Id = 0; Plural =  1; Label = $false }
  'F_'  = @{ Ctx = -1; Id = 0; Plural = -1; Label = $false }
  'FP_' = @{ Ctx = -1; Id = 0; Plural =  1; Label = $false }
}

# One pass over each file: comments, string literals (raw ones too), character literals, keywords followed by '(', and ( , )
$tokenizerPattern =
  '(?<lc>//[^\n]*)' +
  '|(?<bc>/\*.*?\*/)' +
  '|(?<raw>(?:u8|L|u|U)?R"(?<delim>[^()\\\s]{0,16})\((?<rawval>.*?)\)\k<delim>")' +
  '|(?<str>(?:u8|L|u|U)?"(?<strval>(?:[^"\\\n]|\\.)*)")' +
  '|(?<chr>''(?:[^''\\\n]|\\.)+'')' +
  '|(?<kw>(?<![A-Za-z0-9_])(?:FP_|LI_|CL_|NC_|NL_|CW_|C_|L_|W_|N_|P_|F_|_)(?=\s*\())' +
  '|(?<punct>[(),])'
$tokenizer = New-Object -TypeName System.Text.RegularExpressions.Regex -ArgumentList $tokenizerPattern, ([System.Text.RegularExpressions.RegexOptions]::Singleline)

function Write-ExtractWarning ([string] $where, [string] $message)
{
  $script:warnings++
  Write-Warning "${where}: $message"

  # An annotation on the line in GitHub Actions ("file:line")
  if ($env:GITHUB_ACTIONS -eq 'true' -and $where -match '^(?<file>.+):(?<line>\d+)$')
  {
    Write-Host "::warning file=$($Matches.file),line=$($Matches.line)::$message"
  }
}

# Decodes the escape sequences of a C++ string literal (the source is UTF-8; \x and octal escapes are UTF-8 bytes)
function ConvertFrom-CEscapes ([string] $text)
{
  $bytes = New-Object System.Collections.Generic.List[byte]
  $i     = 0

  while ($i -lt $text.Length)
  {
    $ch = $text[$i]

    if ($ch -ne '\' -or $i + 1 -ge $text.Length)
    {
      # Surrogate pairs are encoded as one character
      $len = 1
      if ([char]::IsHighSurrogate($ch) -and $i + 1 -lt $text.Length) { $len = 2 }
      $bytes.AddRange($utf8.GetBytes($text.Substring($i, $len)))
      $i += $len
      continue
    }

    $i++
    $esc = $text[$i]
    $i++

    switch -CaseSensitive ([string] $esc)
    {
      'n'  { $bytes.Add(10) }
      't'  { $bytes.Add(9)  }
      'r'  { $bytes.Add(13) }
      'a'  { $bytes.Add(7)  }
      'b'  { $bytes.Add(8)  }
      'f'  { $bytes.Add(12) }
      'v'  { $bytes.Add(11) }
      'x'  {
        $hex = ''
        while ($i -lt $text.Length -and $text[$i] -match '[0-9A-Fa-f]') { $hex += $text[$i]; $i++ }
        $bytes.Add([byte] ([Convert]::ToInt32($hex, 16) -band 0xFF))
      }
      { $_ -eq 'u' -or $_ -eq 'U' } {
        $digits = 4
        if ($esc -ceq 'U') { $digits = 8 }
        $cp = [Convert]::ToInt32($text.Substring($i, $digits), 16)
        $i += $digits
        $bytes.AddRange($utf8.GetBytes([char]::ConvertFromUtf32($cp)))
      }
      { $_ -match '^[0-7]$' } {
        $oct = [string] $esc
        while ($oct.Length -lt 3 -and $i -lt $text.Length -and $text[$i] -match '[0-7]') { $oct += $text[$i]; $i++ }
        $bytes.Add([byte] ([Convert]::ToInt32($oct, 8) -band 0xFF))
      }
      default { $bytes.AddRange($utf8.GetBytes([string] $esc)) } # \" \' \\ \?
    }
  }

  return $utf8.GetString($bytes.ToArray())
}

# Escapes text for a .po file, splitting it into lines after each \n (as gettext does)
function ConvertTo-PoString ([string] $keyword, [string] $text)
{
  $escaped = $text.Replace('\', '\\').Replace('"', '\"').Replace("`t", '\t').Replace("`r", '\r')
  $lines   = $escaped -split "(?<=`n)"

  if ($lines.Count -le 1 -or ($lines.Count -eq 2 -and $lines[1] -eq ''))
  {
    return "$keyword `"$($escaped.Replace("`n", '\n'))`"`n"
  }

  $out = "$keyword `"`"`n"
  foreach ($line in $lines)
  {
    if ($line -ne '') { $out += "`"$($line.Replace("`n", '\n'))`"`n" }
  }
  return $out
}

$entries = New-Object System.Collections.Specialized.OrderedDictionary

function Add-Entry ($call, [string] $where)
{
  $spec     = $keywords[$call.Keyword]
  $callArgs = $call.Args

  $need = @($spec.Ctx, $spec.Id, $spec.Plural) | Where-Object { $_ -ge 0 }
  foreach ($index in $need)
  {
    if ($index -ge $callArgs.Count -or $null -eq $callArgs[$index])
    {
      Write-ExtractWarning $where "$($call.Keyword)( ) without a string literal as argument $($index + 1); not extracted."
      return
    }
  }

  $ctx    = $null
  $msgid  = $callArgs[$spec.Id]
  $plural = $null
  if ($spec.Ctx    -ge 0) { $ctx    = $callArgs[$spec.Ctx]    }
  if ($spec.Plural -ge 0) { $plural = $callArgs[$spec.Plural] }

  # Labels: only the text before the ID
  if ($spec.Label)
  {
    $pos = $msgid.IndexOf('##')
    if ($pos -ge 0) { $msgid = $msgid.Substring(0, $pos) }
  }

  if ($msgid -eq '')
  {
    return
  }

  if (($msgid + $plural + $ctx) -match '[^\x00-\x7F]')
  {
    Write-ExtractWarning $where "non-ASCII text in `"$msgid`": SKIF is compiled without /utf-8, so text in the sources should be ASCII."
  }

  $key = "$ctx$([char] 4)$msgid"

  if (-not $entries.Contains($key))
  {
    $entries[$key] = [pscustomobject] @{
      Ctx      = $ctx
      MsgId    = $msgid
      Plural   = $plural
      Refs     = New-Object System.Collections.Generic.List[string]
      Comments = New-Object System.Collections.Generic.List[string]
    }
  }

  $entry = $entries[$key]

  if ($null -ne $plural -and $null -eq $entry.Plural) { $entry.Plural = $plural }
  if ($null -ne $entry.Plural -and $null -ne $plural -and $entry.Plural -ne $plural)
  {
    Write-ExtractWarning $where "`"$msgid`" has different plural forms elsewhere (`"$($entry.Plural)`"); keeping that one."
  }

  if (-not $entry.Refs.Contains($where))         { $entry.Refs.Add($where) }
  if ($call.Comment -and -not $entry.Comments.Contains($call.Comment)) { $entry.Comments.Add($call.Comment) }
}

# The files to scan, in a stable order
$files = Get-ChildItem -LiteralPath (Join-Path $Root 'src'), (Join-Path $Root 'include') -Recurse -File |
  Where-Object { $_.Extension -in '.cpp', '.h', '.hpp', '.c' } |
  ForEach-Object {
    [pscustomobject] @{ Full = $_.FullName; Rel = $_.FullName.Substring($Root.Length + 1).Replace('\', '/') }
  } |
  Where-Object { $rel = $_.Rel; -not ($excluded | Where-Object { $rel.StartsWith($_, [StringComparison]::OrdinalIgnoreCase) }) }

# Ordinal order, so the template is the same on every system
$files = @($files)
[string[]] $sortKeys = @($files | ForEach-Object { $_.Rel })
[Array]::Sort($sortKeys, $files, [StringComparer]::OrdinalIgnoreCase)

foreach ($file in $files)
{
  $text = [System.IO.File]::ReadAllText($file.Full, $utf8)

  # Line numbers: offsets of the line starts
  $lineStarts = New-Object System.Collections.Generic.List[int]
  $lineStarts.Add(0)
  for ($i = $text.IndexOf("`n"); $i -ge 0; $i = $text.IndexOf("`n", $i + 1)) { $lineStarts.Add($i + 1) }
  $lineOf = { param ([int] $offset) $index = $lineStarts.BinarySearch($offset); if ($index -lt 0) { $index = (-bnot $index) - 1 }; $index + 1 }

  $stack       = New-Object System.Collections.Generic.List[object] # Calls being parsed (nested calls go on top)
  $comment     = $null                                             # The last TRANSLATORS: comment
  $commentLine = -10
  $prevEnd     = 0

  foreach ($m in $tokenizer.Matches($text))
  {
    $gap     = $text.Substring($prevEnd, $m.Index - $prevEnd)
    $prevEnd = $m.Index + $m.Length
    $top     = $null
    if ($stack.Count -gt 0) { $top = $stack[$stack.Count - 1] }

    # Anything but blanks between the tokens of an argument makes it more than string literals
    if ($null -ne $top -and $top.State -eq 'Args' -and $gap.Trim() -ne '') { $top.Literal = $false }

    if ($m.Groups['lc'].Success -or $m.Groups['bc'].Success)
    {
      $body = $m.Value.Substring(2)
      if ($m.Groups['bc'].Success) { $body = $body.Substring(0, $body.Length - 2) }
      $body = $body.Trim()
      $line = & $lineOf $m.Index

      if ($body.StartsWith('TRANSLATORS:'))
      {
        $comment     = $body
        $commentLine = & $lineOf ($m.Index + $m.Length - 1)
      }
      elseif ($m.Groups['lc'].Success -and $null -ne $comment -and $line -eq $commentLine + 1)
      {
        # A TRANSLATORS: comment continued on the next lines
        $comment     = "$comment $body"
        $commentLine = $line
      }
      continue
    }

    if ($m.Groups['kw'].Success)
    {
      if ($null -ne $top -and $top.State -eq 'Args') { $top.Literal = $false }

      $line = & $lineOf $m.Index
      $call = [pscustomobject] @{
        Keyword = $m.Value; Line = $line; State = 'Paren'; Depth = 0
        Args    = New-Object System.Collections.Generic.List[object]
        Value   = ''; Any = $false; Literal = $true; Comment = $null
      }
      # (on the same line or within the next three, so it can also precede a call that spans several lines)
      if ($null -ne $comment -and $line - $commentLine -le 3 -and $line -ge $commentLine)
      {
        # A comment applies to the next text only
        $call.Comment = $comment
        $comment      = $null
      }

      $stack.Add($call)
      continue
    }

    if ($null -eq $top)
    {
      continue
    }

    if ($top.State -eq 'Paren')
    {
      # The lookahead of the keyword guarantees the '('
      $top.State = 'Args'
      continue
    }

    if ($m.Groups['str'].Success -or $m.Groups['raw'].Success)
    {
      if ($top.Depth -gt 0) { $top.Literal = $false; continue }

      if ($m.Groups['raw'].Success) { $top.Value += $m.Groups['rawval'].Value }
      else                          { $top.Value += ConvertFrom-CEscapes $m.Groups['strval'].Value }
      $top.Any = $true
      continue
    }

    if ($m.Groups['chr'].Success)
    {
      $top.Literal = $false
      continue
    }

    switch ($m.Value)
    {
      '(' { $top.Depth++; $top.Literal = $false }
      ',' {
        if ($top.Depth -eq 0)
        {
          if ($top.Literal -and $top.Any) { $top.Args.Add($top.Value) } else { $top.Args.Add($null) }
          $top.Value = ''; $top.Any = $false; $top.Literal = $true
        }
      }
      ')' {
        if ($top.Depth -gt 0) { $top.Depth--; break }

        if ($top.Literal -and $top.Any) { $top.Args.Add($top.Value) } else { $top.Args.Add($null) }
        $stack.RemoveAt($stack.Count - 1)
        Add-Entry $top "$($file.Rel):$($top.Line)"

        # The call is part of an argument of the call it is nested in
        if ($stack.Count -gt 0) { $stack[$stack.Count - 1].Literal = $false }
      }
    }
  }

  foreach ($call in $stack)
  {
    Write-ExtractWarning "$($file.Rel):$($call.Line)" "unterminated $($call.Keyword)( ); not extracted."
  }
}

# Write the template
$body = New-Object System.Text.StringBuilder

foreach ($entry in $entries.Values)
{
  [void] $body.Append("`n")
  foreach ($c in $entry.Comments) { [void] $body.Append("#. $c`n") }
  foreach ($r in $entry.Refs)     { [void] $body.Append("#: $r`n") }

  if ($null -ne $entry.Ctx) { [void] $body.Append((ConvertTo-PoString 'msgctxt' $entry.Ctx)) }
  [void] $body.Append((ConvertTo-PoString 'msgid' $entry.MsgId))

  if ($null -ne $entry.Plural)
  {
    [void] $body.Append((ConvertTo-PoString 'msgid_plural' $entry.Plural))
    [void] $body.Append("msgstr[0] `"`"`nmsgstr[1] `"`"`n")
  }
  else
  {
    [void] $body.Append("msgstr `"`"`n")
  }
}

$date   = (Get-Date).ToString('yyyy-MM-dd HH:mmzz00')
$header = @"
# Translations of the Special K Injection Frontend (SKIF).
# Generated by tools\l10n_extract.ps1 from the sources; do not edit by hand.
#
#, fuzzy
msgid ""
msgstr ""
"Project-Id-Version: SKIF\n"
"Report-Msgid-Bugs-To: https://github.com/SpecialKO/SKIF/issues\n"
"POT-Creation-Date: $date\n"
"PO-Revision-Date: YEAR-MO-DA HO:MI+ZONE\n"
"Last-Translator: FULL NAME <EMAIL@ADDRESS>\n"
"Language-Team: LANGUAGE <LL@li.org>\n"
"Language: \n"
"MIME-Version: 1.0\n"
"Content-Type: text/plain; charset=UTF-8\n"
"Content-Transfer-Encoding: 8bit\n"
"Plural-Forms: nplurals=INTEGER; plural=EXPRESSION;\n"

"@

$content = ($header.Replace("`r`n", "`n").TrimEnd("`n") + "`n" + $body.ToString())
$path    = Join-Path $Root $Output

# Only rewrite the template if something other than the date changed
$changed  = $true
$existing = $null
$pattern  = '(?m)^"POT-Creation-Date: [^"]*"\r?$'
if (Test-Path -LiteralPath $path)
{
  $existing = [System.IO.File]::ReadAllText($path, $utf8).Replace("`r`n", "`n")
  $changed  = ([regex]::Replace($existing, $pattern, '') -ne [regex]::Replace($content, $pattern, ''))
}

$count = $entries.Count

if ($Check)
{
  # The text only: without the date and the source references (which change whenever code moves)
  $refs     = '(?m)^#: [^\n]*\n'
  $current  = ''
  if ($null -ne $existing) { $current = [regex]::Replace([regex]::Replace($existing, $pattern, ''), $refs, '') }
  $expected = [regex]::Replace([regex]::Replace($content, $pattern, ''), $refs, '')

  $failed = $false

  if ($current -ne $expected)
  {
    $failed  = $true
    $message = "$Output is out of date: the translatable text in the sources changed. Run tools\l10n_extract.ps1 and commit $Output."
    if ($env:GITHUB_ACTIONS -eq 'true') { Write-Host "::error file=$($Output.Replace('\', '/'))::$message" } else { Write-Host $message }

    # Which texts differ (the msgctxt and msgid lines only, to keep it short)
    $was = @($current  -split "`n" | Where-Object { $_ -like 'msgid *' -or $_ -like 'msgctxt *' })
    $now = @($expected -split "`n" | Where-Object { $_ -like 'msgid *' -or $_ -like 'msgctxt *' })
    foreach ($d in (Compare-Object $was $now))
    {
      $what = 'removed:'
      if ($d.SideIndicator -eq '=>') { $what = 'added:  ' }
      Write-Host "  $what $($d.InputObject)"
    }
  }
  else
  {
    Write-Host "$Output is up to date ($count texts from $($files.Count) files)."
  }

  if ($warnings -gt 0)
  {
    $failed = $true
    Write-Host "$warnings warning(s): some texts could not be extracted (see above)."
  }

  exit ([int] $failed)
}

if ($changed)
{
  $folder = Split-Path -Parent $path
  if (-not (Test-Path -LiteralPath $folder)) { [void] (New-Item -ItemType Directory -Path $folder) }
  [System.IO.File]::WriteAllText($path, $content, $utf8)
  Write-Host "Wrote $count texts from $($files.Count) files to $Output."
}
else
{
  Write-Host "$Output is up to date ($count texts from $($files.Count) files)."
}

if ($warnings -gt 0)
{
  Write-Host "$warnings warning(s)."
}
