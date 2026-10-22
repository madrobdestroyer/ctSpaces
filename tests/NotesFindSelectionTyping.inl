struct FindSelectionTyping {ITextFont *duplicate=nullptr;LONG flags=LONG_MIN;FindProbeNativeFont font{};HRESULT status=E_NOINTERFACE;};
static FindSelectionTyping FindCaptureSelectionTyping(HWND edit){
 FindSelectionTyping result;IUnknown *ole=nullptr;ITextDocument *document=nullptr;ITextSelection *selection=nullptr;ITextFont *font=nullptr;ITextFont2 *extended=nullptr;SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(&ole));if(ole){ole->QueryInterface(__uuidof(ITextDocument),reinterpret_cast<void **>(&document));ole->Release();}
 if(document&&SUCCEEDED(document->GetSelection(&selection))&&SUCCEEDED(selection->GetFlags(&result.flags))&&SUCCEEDED(selection->GetFont(&font))){result.status=font->GetDuplicate(&result.duplicate);if(SUCCEEDED(font->QueryInterface(__uuidof(ITextFont2),reinterpret_cast<void **>(&extended)))){extended->GetEffects(&result.font.effects,&result.font.mask);extended->GetCookie(&result.font.cookie);}}
 if(extended)extended->Release();if(font)font->Release();if(selection)selection->Release();if(document)document->Release();return result;
}
static bool FindApplySelectionTyping(ITextDocument *document,const CHARRANGE &range,const FindSelectionTyping &original){
 ITextSelection *selection=nullptr;ITextFont *font=nullptr;bool ok=false;
 if(original.duplicate&&SUCCEEDED(document->GetSelection(&selection))&&SUCCEEDED(selection->SetRange(range.cpMin,range.cpMax))&&SUCCEEDED(selection->SetFlags(original.flags))&&SUCCEEDED(selection->GetFont(&font)))ok=SUCCEEDED(font->SetDuplicate(original.duplicate));
 if(font)font->Release();if(selection)selection->Release();return ok;
}
static bool FindPendingTypingEqual(const CHARFORMAT2W &expected,const CHARFORMAT2W &actual,bool inheritLink){
 auto left=expected,right=actual;if(inheritLink){left.dwEffects&=~(CFE_LINK|CFE_LINKPROTECTED);right.dwEffects&=~(CFE_LINK|CFE_LINKPROTECTED);}
 return FindProbeTypingEqual(left,right)&&(inheritLink||left.dwCookie==right.dwCookie);
}
