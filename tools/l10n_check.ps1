<#
.SYNOPSIS
  Checks the translations of SKIF (resources\l10n\*.po) for mistakes that would make SKIF skip or misshow them.

.DESCRIPTION
  Reads each .po file the way SKIF does, and reports:

  Errors (the check fails):
    * Syntax errors, and files that are not valid UTF-8.
    * A missing Language header, a charset other than UTF-8, or a Plural-Forms header without a valid nplurals.
    * A Language that SKIF ignores: plain English (en), or its pseudo-locale (qps-ploc).
    * Translations that use a {placeholder} the English text does not have, or with unpaired braces
      (SKIF ignores these and shows the English text).
    * Translations whose [links] / [highlights] do not match the English text in number (a [link] that is missing
      or added makes a different link do something else).
    * Plural entries without exactly nplurals forms.
    * Notification titles (marked "at most 63 characters" in SKIF.pot) that are longer (Windows cuts them off).

  Warnings (the check passes):
    * Translations of texts that SKIF no longer has (the file should be updated from SKIF.pot).
    * Texts of SKIF.pot that the file does not have yet (the same).

  Only translated entries are checked (not empty or fuzzy ones, which SKIF shows in English).
  Needs nothing but Windows PowerShell 5.1 (included in Windows) or PowerShell 7 (also on Linux and macOS).
  Used by the l10n workflow (.github\workflows\l10n.yml); in GitHub Actions, problems are also annotated on the line.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\l10n_check.ps1

.EXAMPLE
  pwsh -File tools/l10n_check.ps1 resources/l10n/de.po
#>

[CmdletBinding()]
param (
  # The .po files to check (default: every .po file in resources\l10n)
  [Parameter(ValueFromRemainingArguments = $true)]
  [string[]] $Path,
  # Root of the repository (default: the parent folder of this script)
  [string]   $Root
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Root)) {
  $Root = Split-Path -Parent $PSScriptRoot
}

$Root      = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\', '/')
$utf8      = New-Object System.Text.UTF8Encoding($false, $true) # Throws on invalid UTF-8
$inActions = ($env:GITHUB_ACTIONS -eq 'true')
$errors    = 0
$warnings  = 0

function Get-RelativePath ([string] $file)
{
  $full = (Resolve-Path -LiteralPath $file).Path
  if ($full.StartsWith($Root)) { $full = $full.Substring($Root.Length).TrimStart('\', '/') }
  return $full.Replace('\', '/')
}

function Write-Problem ([string] $kind, [string] $file, [int] $line, [string] $message)
{
  if ($kind -eq 'error') { $script:errors++ } else { $script:warnings++ }

  $where = $file
  if ($line -gt 0) { $where = "${file}:$line" }
  Write-Host "  ${kind}: ${where}: $message"

  if ($inActions)
  {
    $location = "file=$file"
    if ($line -gt 0) { $location += ",line=$line" }
    # Annotations end at a line break
    Write-Host "::$kind $location::$($message.Replace("`n", ' '))"
  }
}

# Decodes the escape sequences of a .po string
function ConvertFrom-PoEscapes ([string] $text)
{
  $sb = New-Object System.Text.StringBuilder
  for ($i = 0; $i -lt $text.Length; $i++)
  {
    $c = $text[$i]
    if ($c -eq '\' -and $i + 1 -lt $text.Length)
    {
      $i++
      switch -CaseSensitive ($text[$i])
      {
        'n'     { [void] $sb.Append("`n") }
        't'     { [void] $sb.Append("`t") }
        'r'     { [void] $sb.Append("`r") }
        '"'     { [void] $sb.Append('"') }
        '\'     { [void] $sb.Append('\') }
        default { [void] $sb.Append('\').Append($text[$i]) }
      }
    }
    else { [void] $sb.Append($c) }
  }
  return $sb.ToString()
}

# Reads a .po file into a list of entries: Ctx, Id, Plural, Strs (index -> text), Fuzzy, Comments (#.), Line.
#   Syntax errors are reported, and the file is read on as far as possible.
function Read-PoFile ([string] $file, [string] $rel)
{
  $bytes = [System.IO.File]::ReadAllBytes($file)
  try   { $text = $utf8.GetString($bytes) }
  catch { Write-Problem 'error' $rel 0 'the file is not valid UTF-8; save it as UTF-8.'; return $null }

  if ($text.Length -gt 0 -and $text[0] -eq [char] 0xFEFF) { $text = $text.Substring(1) }

  $list  = New-Object System.Collections.Generic.List[object]
  $entry = $null
  $field = $null
  $lines = $text.Replace("`r`n", "`n").Split("`n")

  $newEntry = { [pscustomobject] @{ Ctx = $null; Id = $null; Plural = $null; Strs = @{}; Fuzzy = $false; Obsolete = $false; Comments = @(); Line = 0; HasStr = $false } }
  $finish   = {
    if ($null -ne $entry -and $null -ne $entry.Id)
    {
      if (-not $entry.HasStr) { Write-Problem 'error' $rel $entry.Line 'the entry has no msgstr.' }
      $list.Add($entry)
    }
  }

  for ($n = 0; $n -lt $lines.Length; $n++)
  {
    $line = $lines[$n].Trim()
    $num  = $n + 1

    if ($line -eq '')
    {
      & $finish; $entry = $null; $field = $null
      continue
    }

    if ($line.StartsWith('#~')) { continue } # Obsolete entries, kept by gettext tools; SKIF ignores them

    # A comment after the strings of an entry starts the next entry
    if ($line.StartsWith('#') -and $null -ne $entry -and $entry.HasStr)
    {
      & $finish; $entry = $null; $field = $null
    }

    if ($null -eq $entry) { $entry = & $newEntry }

    if ($line.StartsWith('#'))
    {
      if ($line.StartsWith('#,') -and $line -match '\bfuzzy\b') { $entry.Fuzzy = $true }
      elseif ($line.StartsWith('#.'))                           { $entry.Comments += $line.Substring(2).Trim() }
      continue
    }

    if ($line -match '^(?<key>msgctxt|msgid_plural|msgid|msgstr(?:\[(?<idx>\d+)\])?)\s+"(?<val>(?:[^"\\]|\\.)*)"$')
    {
      $value = ConvertFrom-PoEscapes $Matches.val
      $key   = $Matches.key
      $index = 0
      if ($Matches.ContainsKey('idx') -and $Matches.idx) { $index = [int] $Matches.idx }

      if ($key -eq 'msgid' -and ($entry.HasStr -or $null -ne $entry.Id))
      {
        # A new entry without a blank line in between
        & $finish; $entry = & $newEntry
      }

      switch -Regex ($key)
      {
        '^msgctxt$'      { $entry.Ctx    = $value; $field = 'Ctx' }
        '^msgid$'        { $entry.Id     = $value; $field = 'Id'; $entry.Line = $num }
        '^msgid_plural$' { $entry.Plural = $value; $field = 'Plural' }
        '^msgstr'
        {
          # ($Matches is the one of the switch here; $index was taken from the line above)
          if ($null -eq $entry.Id) { Write-Problem 'error' $rel $num 'msgstr without a msgid before it.' }
          $entry.Strs[$index] = $value
          $entry.HasStr       = $true
          $field              = "Str$index"
        }
      }
      continue
    }

    if ($line -match '^"(?<val>(?:[^"\\]|\\.)*)"$' -and $null -ne $field)
    {
      $value = ConvertFrom-PoEscapes $Matches.val
      if ($field.StartsWith('Str')) { $i = [int] $field.Substring(3); $entry.Strs[$i] = $entry.Strs[$i] + $value }
      else                          { $entry.$field = $entry.$field + $value }
      continue
    }

    Write-Problem 'error' $rel $num "syntax error: $($lines[$n].Trim())"
    $field = $null
  }

  & $finish
  return ,$list
}

function Get-Key ($entry)
{
  if ($null -ne $entry.Ctx) { return "$($entry.Ctx)$([char] 4)$($entry.Id)" } # As gettext: msgctxt EOT msgid
  return $entry.Id
}

# Placeholders ({0}, {1:>3}, ...) of a text, as SKIF finds them; $null if the braces do not pair up ("{{" and "}}" are literal)
function Get-Placeholders ([string] $text)
{
  $found = New-Object System.Collections.Generic.List[string]
  for ($i = 0; $i -lt $text.Length; $i++)
  {
    if ($text[$i] -eq '{')
    {
      if ($i + 1 -lt $text.Length -and $text[$i + 1] -eq '{') { $i++; continue }
      $close = $text.IndexOf('}', $i)
      if ($close -lt 0) { return $null }
      $found.Add($text.Substring($i, $close - $i + 1))
      $i = $close
    }
    elseif ($text[$i] -eq '}')
    {
      if ($i + 1 -lt $text.Length -and $text[$i + 1] -eq '}') { $i++; continue }
      return $null
    }
  }
  return ,$found
}

# The number of [links] / [highlights] of a text ("[[" and "]]" are literal brackets)
function Get-BracketCount ([string] $text)
{
  return ([regex]::Matches($text.Replace('[[', '').Replace(']]', ''), '\[')).Count
}

# SKIF.pot: the texts, and their comments
$potPath = Join-Path $Root 'resources/l10n/SKIF.pot'
$pot     = New-Object System.Collections.Hashtable ([System.StringComparer]::Ordinal) # Case-sensitive, as SKIF
if (Test-Path -LiteralPath $potPath)
{
  foreach ($e in (Read-PoFile $potPath 'resources/l10n/SKIF.pot'))
  {
    if ($e.Id -ne '') { $pot[(Get-Key $e)] = $e }
  }
}
else
{
  Write-Host "resources/l10n/SKIF.pot not found; checking the files on their own."
}

if (-not $Path -or $Path.Count -eq 0)
{
  $Path = @(Get-ChildItem -LiteralPath (Join-Path $Root 'resources/l10n') -Filter '*.po' -File | Sort-Object Name | ForEach-Object { $_.FullName })
}

if ($Path.Count -eq 0)
{
  Write-Host 'No translations (.po files) to check.'
  exit 0
}

foreach ($file in $Path)
{
  $rel = Get-RelativePath $file
  Write-Host $rel

  $before  = $errors
  $entries = Read-PoFile $file $rel
  if ($null -eq $entries) { continue }

  # Header
  $header   = $entries | Where-Object { $_.Id -eq '' -and $null -eq $_.Ctx } | Select-Object -First 1
  $nplurals = 0
  if ($null -eq $header)
  {
    Write-Problem 'error' $rel 1 'the header (the entry with an empty msgid) is missing.'
  }
  else
  {
    $fields = @{}
    foreach ($l in ([string] $header.Strs[0]).Split("`n"))
    {
      $colon = $l.IndexOf(':')
      if ($colon -gt 0) { $fields[$l.Substring(0, $colon).Trim().ToLowerInvariant()] = $l.Substring($colon + 1).Trim() }
    }

    if (-not $fields.ContainsKey('language') -or $fields['language'] -eq '')
    {
      Write-Problem 'error' $rel $header.Line 'the Language header is missing (e.g. "Language: de" or "Language: pt_BR").'
    }
    else
    {
      # The language as SKIF reads it: pt_BR -> pt-BR, sr@latin -> sr-Latn, en@pirate -> en-x-pirate
      $tag = $fields['language']
      $at  = $tag.IndexOf('@')
      if ($at -ge 0)
      {
        $modifier = $tag.Substring($at + 1).ToLowerInvariant()
        $tag      = $tag.Substring(0, $at)
        if     ($modifier -eq 'latin')    { $tag += '-Latn' }
        elseif ($modifier -eq 'cyrillic') { $tag += '-Cyrl' }
        elseif ($modifier -ne '')         { $tag += "-x-$modifier" }
      }
      $tag = $tag.Replace('_', '-')

      if ($tag.ToLowerInvariant() -eq 'en' -or $tag -eq 'qps-ploc')
      {
        Write-Problem 'error' $rel $header.Line "SKIF ignores a translation with the language `"$($fields['language'])`" ($tag is SKIF's own English, or its pseudo-locale)."
      }
    }

    if ($fields.ContainsKey('content-type') -and $fields['content-type'] -match 'charset=(?<cs>[^;\s]+)' -and $Matches.cs -notmatch '^utf-?8$')
    {
      Write-Problem 'error' $rel $header.Line "the charset is $($Matches.cs); SKIF reads UTF-8 only (Content-Type: text/plain; charset=UTF-8)."
    }

    if (-not $fields.ContainsKey('plural-forms') -or $fields['plural-forms'] -notmatch 'nplurals\s*=\s*(?<n>\d+)')
    {
      Write-Problem 'error' $rel $header.Line 'the Plural-Forms header is missing or has no nplurals (e.g. "Plural-Forms: nplurals=2; plural=(n != 1);").'
    }
    else
    {
      $nplurals = [int] $Matches.n
      if ($nplurals -lt 1 -or $nplurals -gt 6) { Write-Problem 'error' $rel $header.Line "nplurals=$nplurals is out of range (1 to 6)." }
    }
  }

  $total      = 0
  $translated = 0
  $unknown    = 0
  $seen       = New-Object System.Collections.Hashtable ([System.StringComparer]::Ordinal)

  foreach ($e in $entries)
  {
    if ($e.Id -eq '' -and $null -eq $e.Ctx) { continue }

    $key        = Get-Key $e
    $seen[$key] = $true
    $total++

    $potEntry = $null
    if ($pot.Count -gt 0)
    {
      if ($pot.ContainsKey($key)) { $potEntry = $pot[$key] }
      else                        { $unknown++ }
    }

    $strs = @($e.Strs.Keys | Sort-Object | ForEach-Object { $e.Strs[$_] })
    if ($e.Fuzzy -or $strs.Count -eq 0 -or @($strs | Where-Object { $_ -ne '' }).Count -eq 0) { continue }
    $translated++

    # Plural forms
    if ($null -ne $e.Plural -and $nplurals -gt 0 -and $e.Strs.Count -ne $nplurals)
    {
      Write-Problem 'error' $rel $e.Line "$($e.Strs.Count) plural form(s), but the Plural-Forms header has nplurals=$nplurals."
    }

    $english  = $e.Id
    if ($null -ne $e.Plural) { $english += $e.Plural }
    $allowed  = Get-Placeholders $english

    foreach ($index in ($e.Strs.Keys | Sort-Object))
    {
      $str = $e.Strs[$index]
      if ($str -eq '') { continue }

      # Placeholders (only if the English text is a pattern, as in SKIF)
      if ($null -ne $allowed)
      {
        $used = Get-Placeholders $str
        if ($null -eq $used)
        {
          Write-Problem 'error' $rel $e.Line "the translation of `"$($e.Id)`" has unpaired braces { }; SKIF shows the English text instead (use {{ and }} for literal braces)."
        }
        else
        {
          $extra = @($used | Where-Object { -not $allowed.Contains($_) } | Select-Object -Unique)
          if ($extra.Count -gt 0)
          {
            Write-Problem 'error' $rel $e.Line "the translation of `"$($e.Id)`" uses $($extra -join ', '), which the English text does not have; SKIF shows the English text instead."
          }
        }
      }

      # [links] / [highlights]
      $want = Get-BracketCount $e.Id
      $have = Get-BracketCount $str
      if ($want -ne $have)
      {
        Write-Problem 'error' $rel $e.Line "the translation of `"$($e.Id)`" has $have [bracketed] part(s), the English text $want; keep each [link] / [highlight] (translate the text inside the brackets)."
      }

      # Notification titles
      if ($null -ne $potEntry -and ($potEntry.Comments -join ' ') -match 'at most (?<max>\d+) characters')
      {
        $max = [int] $Matches.max
        # Characters as Windows counts them (UTF-16)
        if ($str.Length -gt $max)
        {
          Write-Problem 'error' $rel $e.Line "the translation of `"$($e.Id)`" is $($str.Length) characters long; it may be at most $max (it is the title of a notification)."
        }
      }
    }
  }

  # Against SKIF.pot
  $missing = 0
  if ($pot.Count -gt 0)
  {
    foreach ($k in $pot.Keys) { if (-not $seen.ContainsKey($k)) { $missing++ } }

    if ($unknown -gt 0 -or $missing -gt 0)
    {
      $parts = @()
      if ($missing -gt 0) { $parts += "$missing text(s) of SKIF.pot are missing" }
      if ($unknown -gt 0) { $parts += "$unknown text(s) are no longer in SKIF" }
      Write-Problem 'warning' $rel 0 "$($parts -join ', '); update the file from SKIF.pot (Poedit: Translation > Update from POT file)."
    }
  }

  $of = $total
  if ($pot.Count -gt 0) { $of = $pot.Count }
  $percent = 0
  if ($of -gt 0) { $percent = [math]::Floor($translated * 100 / $of) }
  $result = 'OK'
  if ($errors -gt $before) { $result = "$($errors - $before) error(s)" }
  Write-Host "  $translated of $of texts translated ($percent%): $result"
}

Write-Host ''
Write-Host "$($Path.Count) file(s) checked: $errors error(s), $warnings warning(s)."
exit ([int] ($errors -gt 0))
