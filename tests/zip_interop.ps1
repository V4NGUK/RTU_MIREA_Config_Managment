param([string]$Emulator, [string]$Fixtures, [string]$Work)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
New-Item -ItemType Directory -Path $Work -Force | Out-Null
$sourceZip = Join-Path $Fixtures 'commands.zip'
$saved = Join-Path $Work 'saved archive.zip'
$script = Join-Path $Work 'commands.emu'
$hash = (Get-FileHash -LiteralPath $sourceZip).Hash
$scriptText = @"
cd /home/alex
touch new.txt
chmod 600 new.txt
chmod 700 /
vfs-save "$($saved.Replace('\', '/'))"
exit
"@
[System.IO.File]::WriteAllText($script, $scriptText, [System.Text.UTF8Encoding]::new($false))
$result = & $Emulator --vfs $sourceZip --script $script 2>&1
if ($LASTEXITCODE -ne 0) { throw ($result -join "`n") }
$archive = [System.IO.Compression.ZipFile]::OpenRead($saved)
try {
    $entry = $archive.GetEntry('home/alex/new.txt')
    if (-not $entry -or $entry.Length -ne 0) { throw 'touch: файл не сохранён' }
    if ((($entry.ExternalAttributes -shr 16) -band 0xFFF) -ne 384) { throw 'chmod: неверный режим' }
    $root = $archive.GetEntry('./')
    if (-not $root -or (($root.ExternalAttributes -shr 16) -band 0xFFF) -ne 448) {
        throw 'Не сохранены права корня'
    }
    $reader = [System.IO.StreamReader]::new($archive.GetEntry('home/alex/notes.txt').Open())
    try {
        if ($reader.ReadToEnd() -ne "first`nsecond`nthird`n") { throw 'Изменились данные ZIP' }
    } finally { $reader.Dispose() }
} finally { $archive.Dispose() }
if ((Get-FileHash -LiteralPath $sourceZip).Hash -ne $hash) { throw 'Исходный ZIP изменён' }
$compressed = Join-Path $Work 'deflated.zip'
$stream = [System.IO.File]::Open($compressed, [System.IO.FileMode]::Create)
$archive = [System.IO.Compression.ZipArchive]::new($stream, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    $entry = $archive.CreateEntry('deflated.txt', [System.IO.Compression.CompressionLevel]::Optimal)
    $writer = [System.IO.StreamWriter]::new($entry.Open(), [System.Text.UTF8Encoding]::new($false))
    try { $writer.Write("compressed`ndata`n") } finally { $writer.Dispose() }
} finally { $archive.Dispose(); $stream.Dispose() }
[System.IO.File]::WriteAllText($script, "tac /deflated.txt`nexit`n", [System.Text.UTF8Encoding]::new($false))
$result = & $Emulator --vfs $compressed --script $script 2>&1
if ($LASTEXITCODE -ne 0 -or ($result -join "`n") -notmatch 'data\r?\ncompressed') {
    throw "Не прочитан Deflate-архив .NET: $result"
}
Write-Output 'ZIP открыт независимой библиотекой .NET: содержимое и права сохранены.'
