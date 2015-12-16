#include "Utils.h"

using namespace clang;

namespace Utils {

// This function adapted from clang/lib/ARCMigrate/Transforms.cpp
SourceLocation
findSemiAfterLocation(SourceManager & SM,
                      const LangOptions & LangOpts,
                      SourceLocation loc,
                      int Offset)
{
    if (loc.isMacroID()) {
        if (!Lexer::isAtEndOfMacroExpansion(loc, SM, LangOpts, &loc))
            return SourceLocation();
    }
    loc = Lexer::getLocForEndOfToken(loc, Offset, SM, LangOpts);
    
    // Break down the source location.
    std::pair<FileID, unsigned> locInfo = SM.getDecomposedLoc(loc);
    
    // Try to load the file buffer.
    bool invalidTemp = false;
    StringRef file = SM.getBufferData(locInfo.first, &invalidTemp);
    if (invalidTemp)
        return SourceLocation();
    
    const char *tokenBegin = file.data() + locInfo.second;
    
    // Lex from the start of the given location.
    Lexer lexer(SM.getLocForStartOfFile(locInfo.first),
                LangOpts,
                file.begin(), tokenBegin, file.end());
    Token tok;
    lexer.LexFromRawLexer(tok);
    if (tok.isNot(tok::semi))
        return SourceLocation();
    return tok.getLocation();
}

SourceRange
expandRange(SourceManager & SM,
            const LangOptions & LangOpts,
            SourceRange r)
{
    // If the range is a full statement, and is followed by a
    // semi-colon then expand the range to include the semicolon.
    SourceLocation b = r.getBegin();
    SourceLocation e = findSemiAfterLocation(SM, LangOpts, r.getEnd());
    if (e.isInvalid()) e = r.getEnd();
    return SourceRange(b,e);
}

SourceRange expandSpellingLocationRange(SourceManager & SM,
                                        const clang::LangOptions & LangOpts,
                                        clang::SourceRange r)
{
    return expandRange(SM,
                       LangOpts,
                       getSpellingLocationRange(SM, r));
}

SourceRange getSpellingLocationRange(SourceManager & sm,
                                     SourceRange r)
{
    SourceLocation sb = sm.getSpellingLoc(r.getBegin());
    SourceLocation se = sm.getSpellingLoc(r.getEnd());
    return SourceRange(sb, se);
}

SourceRange getImmediateMacroArgCallerRange(SourceManager & sm,
                                            SourceRange r)
{
    SourceLocation b = r.getBegin();
    SourceLocation e = r.getEnd();

    if (b.isMacroID() && e.isMacroID() &&
        sm.isMacroArgExpansion(b) && sm.isMacroArgExpansion(e))
    {
        b = sm.getImmediateMacroCallerLoc(r.getBegin());
        e = sm.getImmediateMacroCallerLoc(r.getEnd());
    }

    return SourceRange(b, e);
}

bool SelectRange(SourceManager & SM,
                 FileID mainFileID,
                 SourceRange r)
{
    FullSourceLoc loc = FullSourceLoc(r.getEnd(), SM);
    return SM.isInMainFile(loc) && 
           !SM.isMacroBodyExpansion(loc);
}


bool ShouldVisitStmt(SourceManager & SM,
                     const LangOptions & LangOpts,
                     FileID mainFileID,
                     clang::Stmt * stmt)
{
    SourceRange r;

    if (stmt->getStmtClass() == Stmt::NoStmtClass) {
        return false;
    }
    else {    
        r = expandRange(SM, LangOpts, stmt->getSourceRange());
        return SelectRange(SM, mainFileID, r);
    }
}

bool ShouldAssociateBytesWithStmt(Stmt *S, Stmt *P)
{
    if ( S != NULL )
    {
        if (S->getStmtClass() == Stmt::BreakStmtClass ||
            S->getStmtClass() == Stmt::CapturedStmtClass ||
            S->getStmtClass() == Stmt::CompoundStmtClass ||
            S->getStmtClass() == Stmt::ContinueStmtClass ||
            S->getStmtClass() == Stmt::CXXCatchStmtClass ||
            S->getStmtClass() == Stmt::CXXForRangeStmtClass ||
            S->getStmtClass() == Stmt::CXXTryStmtClass ||
            S->getStmtClass() == Stmt::DeclStmtClass ||
            S->getStmtClass() == Stmt::DoStmtClass ||
            S->getStmtClass() == Stmt::ForStmtClass ||
            S->getStmtClass() == Stmt::GotoStmtClass ||
            S->getStmtClass() == Stmt::IfStmtClass ||
            S->getStmtClass() == Stmt::IndirectGotoStmtClass ||
            S->getStmtClass() == Stmt::ReturnStmtClass ||
            S->getStmtClass() == Stmt::SwitchStmtClass ||
            S->getStmtClass() == Stmt::DefaultStmtClass ||
            S->getStmtClass() == Stmt::CaseStmtClass ||
            S->getStmtClass() == Stmt::WhileStmtClass ||
            IsSingleLineStmt(S, P)) {
            return true;
        }
    }

    return false;
}

// Return true if the clang::Stmt is a statement in the C++ grammar
// which would typically be a single line in a program.
// This is done by testing if the parent of the clang::Stmt
// is an aggregation type.  The immediate children of an aggregation
// type are all valid statements in the C/C++ grammar.
bool IsSingleStmt(Stmt *S, Stmt *P)
{
    if (S != NULL && P != NULL) {
        switch (P->getStmtClass()){
        case Stmt::CompoundStmtClass:
        {
            return true;
        }
        case Stmt::CapturedStmtClass:
        {
            CapturedStmt * cs = static_cast<CapturedStmt*>(P);
            return cs->getCapturedStmt() == S;
        }
        case Stmt::CXXCatchStmtClass:
        {
            CXXCatchStmt * cs = static_cast<CXXCatchStmt*>(P);
            return cs->getHandlerBlock() == S;
        }
        case Stmt::CXXForRangeStmtClass:
        {
            CXXForRangeStmt * fs = static_cast<CXXForRangeStmt*>(P);
            return fs->getBody() == S;
        }
        case Stmt::DoStmtClass:
        {
            DoStmt *ds = static_cast<DoStmt*>(P);
            return ds->getBody() == S;
        }
        case Stmt::ForStmtClass:
        {
            ForStmt *fs = static_cast<ForStmt*>(P);
            return fs->getBody() == S;
        }
        case Stmt::IfStmtClass:
        {
            IfStmt *is = static_cast<IfStmt*>(P);
            return is->getThen() == S || is->getElse() == S;
        }
        case Stmt::SwitchStmtClass:
        {
            SwitchStmt *ss = static_cast<SwitchStmt*>(P); 
            return ss->getBody() == S;
        }
        case Stmt::WhileStmtClass:
        {
            WhileStmt *ws = static_cast<WhileStmt*>(P);
            return ws->getBody() == S;
        }
        default:
            return false;
        }
    }

    return false;
}

// Return true if S is a top-level statement within a 
// loop/if conditional.
bool IsGuardStmt(Stmt *S, Stmt *P)
{
    if (!IsSingleLineStmt(S, P) && P != NULL &&
        (P->getStmtClass() == Stmt::CapturedStmtClass ||
         P->getStmtClass() == Stmt::CompoundStmtClass || 
         P->getStmtClass() == Stmt::CXXCatchStmtClass ||
         P->getStmtClass() == Stmt::CXXForRangeStmtClass ||
         P->getStmtClass() == Stmt::CXXTryStmtClass ||
         P->getStmtClass() == Stmt::DoStmtClass ||
         P->getStmtClass() == Stmt::ForStmtClass ||
         P->getStmtClass() == Stmt::IfStmtClass ||
         P->getStmtClass() == Stmt::SwitchStmtClass ||
         P->getStmtClass() == Stmt::WhileStmtClass)) {
         return true;
    }

    return false;
}

}
