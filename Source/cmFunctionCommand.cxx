/*=========================================================================

  Program:   CMake - Cross-Platform Makefile Generator
  Module:    $RCSfile$
  Language:  C++
  Date:      $Date$
  Version:   $Revision$

  Copyright (c) 2002 Kitware, Inc., Insight Consortium.  All rights reserved.
  See Copyright.txt or http://www.cmake.org/HTML/Copyright.html for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notices for more information.

=========================================================================*/
#include "cmFunctionCommand.h"

#include "cmake.h"

// define the class for function commands
class cmFunctionHelperCommand : public cmCommand
{
public:
  cmFunctionHelperCommand() {}

  ///! clean up any memory allocated by the function
  ~cmFunctionHelperCommand() {};

  /**
   * This is a virtual constructor for the command.
   */
  virtual cmCommand* Clone()
  {
    cmFunctionHelperCommand *newC = new cmFunctionHelperCommand;
    // we must copy when we clone
    newC->Args = this->Args;
    newC->Functions = this->Functions;
    return newC;
  }

  /**
   * This determines if the command is invoked when in script mode.
   */
  virtual bool IsScriptable() { return true; }

  /**
   * This is called when the command is first encountered in
   * the CMakeLists.txt file.
   */
  virtual bool InvokeInitialPass(const std::vector<cmListFileArgument>& args);

  virtual bool InitialPass(std::vector<std::string> const&) { return false; };

  /**
   * The name of the command as specified in CMakeList.txt.
   */
  virtual const char* GetName() { return this->Args[0].c_str(); }
  
  /**
   * Succinct documentation.
   */
  virtual const char* GetTerseDocumentation()
  {
    std::string docs = "Function named: ";
    docs += this->GetName();
    return docs.c_str();
  }

  /**
   * More documentation.
   */
  virtual const char* GetFullDocumentation()
  {
    return this->GetTerseDocumentation();
  }

  cmTypeMacro(cmFunctionHelperCommand, cmCommand);

  std::vector<std::string> Args;
  std::vector<cmListFileFunction> Functions;
};


bool cmFunctionHelperCommand::InvokeInitialPass
(const std::vector<cmListFileArgument>& args)
{
  // Expand the argument list to the function.
  std::vector<std::string> expandedArgs;
  this->Makefile->ExpandArguments(args, expandedArgs);

  // make sure the number of arguments passed is at least the number
  // required by the signature
  if (expandedArgs.size() < this->Args.size() - 1)
    {
    std::string errorMsg =
      "Function invoked with incorrect arguments for function named: ";
    errorMsg += this->Args[0];
    this->SetError(errorMsg.c_str());
    return false;
    }

  // we push a scope on the makefile
  this->Makefile->PushScope();

  // set the value of argc
  cmOStringStream strStream;
  strStream << expandedArgs.size();
  this->Makefile->AddDefinition("ARGC",strStream.str().c_str());

  // set the values for ARGV0 ARGV1 ...
  for (unsigned int t = 0; t < expandedArgs.size(); ++t)
    {
    strStream.str("");
    strStream << "ARGV" << t;
    this->Makefile->AddDefinition(strStream.str().c_str(), 
                                  expandedArgs[t].c_str());
    }
  
  // define the formal arguments
  for (unsigned int j = 1; j < this->Args.size(); ++j)
    {
    this->Makefile->AddDefinition(this->Args[j].c_str(), 
                                  expandedArgs[j-1].c_str());
    }

  // define ARGV and ARGN
  std::vector<std::string>::const_iterator eit;
  std::string argvDef;
  std::string argnDef;
  unsigned int cnt = 0;
  for ( eit = expandedArgs.begin(); eit != expandedArgs.end(); ++eit )
    {
    if ( argvDef.size() > 0 )
      {
      argvDef += ";";
      }
    argvDef += *eit;
    if ( cnt >= this->Args.size()-1 )
      {
      if ( argnDef.size() > 0 )
        {
        argnDef += ";";
        }
      argnDef += *eit;
      }
    cnt ++;
    }
  this->Makefile->AddDefinition("ARGV", argvDef.c_str());
  this->Makefile->AddDefinition("ARGN", argnDef.c_str());

  // Invoke all the functions that were collected in the block.
  cmListFileFunction newLFF;

  // for each function
  for(unsigned int c = 0; c < this->Functions.size(); ++c)
    {
    if (!this->Makefile->ExecuteCommand(this->Functions[c]))
      {
      cmOStringStream error;
      error << "Error in cmake code at\n"
            << this->Functions[c].FilePath << ":" 
            << this->Functions[c].Line << ":\n"
            << "A command failed during the invocation of function \""
            << this->Args[0].c_str() << "\".";
      cmSystemTools::Error(error.str().c_str());

      // pop scope on the makefile and return
      this->Makefile->PopScope();
      return false;
      }
    }

  // pop scope on the makefile
  this->Makefile->PopScope();
  return true;
}

bool cmFunctionFunctionBlocker::
IsFunctionBlocked(const cmListFileFunction& lff, cmMakefile &mf)
{
  // record commands until we hit the ENDFUNCTION
  // at the ENDFUNCTION call we shift gears and start looking for invocations
  if(!cmSystemTools::Strucmp(lff.Name.c_str(),"function"))
    {
    this->Depth++;
    }
  else if(!cmSystemTools::Strucmp(lff.Name.c_str(),"endfunction"))
    {
    // if this is the endfunction for this function then execute
    if (!this->Depth) 
      {
      std::string name = this->Args[0];
      std::vector<std::string>::size_type cc;
      name += "(";
      for ( cc = 0; cc < this->Args.size(); cc ++ )
        {
        name += " " + this->Args[cc];
        }
      name += " )";

      // create a new command and add it to cmake
      cmFunctionHelperCommand *f = new cmFunctionHelperCommand();
      f->Args = this->Args;
      f->Functions = this->Functions;
      std::string newName = "_" + this->Args[0];
      mf.GetCMakeInstance()->RenameCommand(this->Args[0].c_str(), 
                                           newName.c_str());
      mf.AddCommand(f);

      // remove the function blocker now that the function is defined
      mf.RemoveFunctionBlocker(lff);
      return true;
      }
    else
      {
      // decrement for each nested function that ends
      this->Depth--;
      }
    }

  // if it wasn't an endfunction and we are not executing then we must be
  // recording
  this->Functions.push_back(lff);
  return true;
}


bool cmFunctionFunctionBlocker::
ShouldRemove(const cmListFileFunction& lff, cmMakefile &mf)
{
  if(!cmSystemTools::Strucmp(lff.Name.c_str(),"endfunction"))
    {
    std::vector<std::string> expandedArguments;
    mf.ExpandArguments(lff.Arguments, expandedArguments);
    if ((!expandedArguments.empty() && 
        (expandedArguments[0] == this->Args[0]))
        || cmSystemTools::IsOn
        (mf.GetPropertyOrDefinition("CMAKE_ALLOW_LOOSE_LOOP_CONSTRUCTS")))
      {
      return true;
      }
    }

  return false;
}

void cmFunctionFunctionBlocker::
ScopeEnded(cmMakefile &mf)
{
  // functions should end with an EndFunction
  cmSystemTools::Error(
    "The end of a CMakeLists file was reached with a FUNCTION statement that "
    "was not closed properly. Within the directory: ",
    mf.GetCurrentDirectory(), " with function ",
    this->Args[0].c_str());
}

bool cmFunctionCommand::InitialPass(std::vector<std::string> const& args)
{
  if(args.size() < 1)
    {
    this->SetError("called with incorrect number of arguments");
    return false;
    }

  // create a function blocker
  cmFunctionFunctionBlocker *f = new cmFunctionFunctionBlocker();
  for(std::vector<std::string>::const_iterator j = args.begin();
      j != args.end(); ++j)
    {   
    f->Args.push_back(*j);
    }
  this->Makefile->AddFunctionBlocker(f);
  return true;
}
