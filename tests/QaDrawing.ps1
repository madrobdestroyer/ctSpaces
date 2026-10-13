function Get-QaDrawingReferences {
 if($PSVersionTable.PSEdition -eq 'Core') {
  Add-Type -AssemblyName System.Drawing.Common
  @((Get-ChildItem -LiteralPath (Join-Path $PSHOME 'ref') -Filter '*.dll').FullName)
  [Drawing.Bitmap].Assembly.Location
  @((Get-ChildItem -LiteralPath $PSHOME -Filter 'System.Private.Windows.*.dll').FullName)
 } else { 'System.Drawing' }
}
