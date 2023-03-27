/*
 * This source file is part of an OSTIS project. For the latest info, see http://ostis.net
 * Distributed under the MIT License
 * (See accompanying file COPYING.MIT or copy at http://opensource.org/licenses/MIT)
 */

#include "sc_component_manager_command_install.hpp"
#include <sc-memory/utils/sc_exec.hpp>
#include <sc-builder/src/scs_loader.hpp>
#include "src/manager/utils/sc_component_utils.hpp"

#include "src/manager/commands/command_init/constants/command_init_constants.hpp"

ScComponentManagerCommandInstall::ScComponentManagerCommandInstall(std::string specificationsPath)
  : m_specificationsPath(std::move(specificationsPath))
{
}

ExecutionResult ScComponentManagerCommandInstall::Execute(
    ScMemoryContext * context,
    CommandParameters const & commandParameters)
{
  ExecutionResult result;
  std::vector<std::string> componentsToInstall;

  try
  {
    componentsToInstall = commandParameters.at(PARAMETER_NAME);
  }
  catch (std::exception const & exception)
  {
    // TODO: Implement install all components method
    SC_LOG_INFO("No identifier provided, installing all to install components");

    return result;
  }

  componentsToInstall = commandParameters.at(PARAMETER_NAME);
  for (std::string const & componentToInstallIdentifier : componentsToInstall)
  {
    ScAddr componentAddr = context->HelperFindBySystemIdtf(componentToInstallIdentifier);

    SC_LOG_DEBUG("Validating component \"" + componentToInstallIdentifier);
    if (!ValidateComponent(context, componentAddr))
    {
      SC_LOG_WARNING("Unable to install component \"" + componentToInstallIdentifier);
      continue;
    }
    SC_LOG_DEBUG("Component \"" + componentToInstallIdentifier + "\" is specified correctly");

    InstallDependencies(context, componentAddr);

    DownloadComponent(context, componentAddr);

    // TODO: need to process installation method from component specification in kb
  }

  return result;
}

/**
 * @brief Checks if component is valid.
 * Checks if:
 * - component exist;
 * - component is reusable;
 * - component's address link is valid;
 * - component's installation method is valid;
 * @return Returns true if component is valid.
 */
bool ScComponentManagerCommandInstall::ValidateComponent(ScMemoryContext * context, ScAddr const & componentAddr)
{
  // Check if component exist
  if (!componentAddr.IsValid())
  {
    SC_LOG_WARNING("Component not found. Unable to install");
    return false;
  }

  // Check if component is a reusable component
  ScIterator3Ptr const reusableComponentCLassIterator = context->Iterator3(
      keynodes::ScComponentManagerKeynodes::concept_reusable_component, ScType::EdgeAccessConstPosPerm, componentAddr);
  if (!reusableComponentCLassIterator->Next())
  {
    SC_LOG_WARNING("Component is not a reusable component.");
    return false;
  }

  ScAddr componentAddressAddr;
  // Find and check component address
  try
  {
    componentAddressAddr = componentUtils::SearchUtils::GetComponentAddress(context, componentAddr);
  }
  catch (utils::ScException const & exception)
  {
    SC_LOG_ERROR(exception.Message());
    SC_LOG_ERROR(exception.Description());
  }

  std::string componentAddressContent;
  context->GetLinkContent(componentAddressAddr, componentAddressContent);
  if (componentAddressContent.empty())
  {
    SC_LOG_WARNING("Component address not found.");
    return false;
  }

  ScAddr componentInstallationMethod;

  // Find and check component installation method
  try
  {
    componentInstallationMethod = componentUtils::SearchUtils::GetComponentInstallationMethod(context, componentAddr);
  }
  catch (utils::ScException const & exception)
  {
    SC_LOG_ERROR(exception.Message());
    SC_LOG_ERROR(exception.Description());
  }

  if (!componentInstallationMethod.IsValid())
  {
    SC_LOG_WARNING("Component installation method not found.");
    return false;
  }

  return true;
}

/**
 * Tries to install component dependencies.
 * @return Returns {DependencyIdtf1, DependencyIdtf2, ...}
 * if installation successfull, otherwise
 * returns empty vector.
 */
ExecutionResult ScComponentManagerCommandInstall::InstallDependencies(
    ScMemoryContext * context,
    ScAddr const & componentAddr)
{
  ExecutionResult result;
  // Get component dependencies and install them recursively

  ScAddrVector componentDependencies;

  try
  {
    componentDependencies = componentUtils::SearchUtils::GetComponentDependencies(context, componentAddr);
  }
  catch (utils::ScException const & exception)
  {
    SC_LOG_ERROR(exception.Message());
    SC_LOG_ERROR(exception.Description());
  }

  for (ScAddr const & componentDependency : componentDependencies)
  {
    std::string dependencyIdtf = context->HelperGetSystemIdtf(componentDependency);
    SC_LOG_INFO("ScComponentManager: Install dependency \"" + dependencyIdtf + "\"");
    CommandParameters dependencyParameters = {{PARAMETER_NAME, {dependencyIdtf}}};
    ExecutionResult dependencyResult = Execute(context, dependencyParameters);

    // Return empty if you couldn't install dependency
    if (dependencyResult.empty())
    {
      SC_LOG_ERROR("Dependency \"" + dependencyIdtf + "\" is not installed");
      return dependencyResult;
    }
    result.insert(result.cbegin(), dependencyResult.cbegin(), dependencyResult.cend());
  }

  return result;
}

/**
 * Tries to download component from Github
 */
void ScComponentManagerCommandInstall::DownloadComponent(ScMemoryContext * context, ScAddr const & componentAddr)
{
  ScAddr componentAddressAddr;

  try
  {
    componentAddressAddr = componentUtils::SearchUtils::GetComponentAddress(context, componentAddr);
  }
  catch (utils::ScException const & exception)
  {
    SC_LOG_ERROR(exception.Message());
    SC_LOG_ERROR(exception.Description());
  }
  std::string componentAddressContent;
  context->GetLinkContent(componentAddressAddr, componentAddressContent);
  if (componentAddressContent.find(GitHubConstants::GITHUB_PREFIX) != std::string::npos)
  {
    struct stat sb
    {
    };
    size_t componentDirNameIndex = componentAddressContent.rfind('/');
    std::string componentDirName = m_specificationsPath + componentAddressContent.substr(componentDirNameIndex);
    while (stat(componentDirName.c_str(), &sb) == 0)
    {
      componentDirName += componentAddressContent.substr(componentDirNameIndex);
    }
    sc_fs_mkdirs(componentDirName.c_str());

    ScExec exec{{"cd", componentDirName, "&&", "git clone ", componentAddressContent}};

    // TODO: add dir and diread deletion

    ScsLoader loader;
    DIR * dir;
    struct dirent * diread;
    componentDirName += componentAddressContent.substr(componentDirNameIndex);
    if ((dir = opendir(componentDirName.c_str())) != nullptr)
    {
      while ((diread = readdir(dir)) != nullptr)
      {
        std::string filename = diread->d_name;
        if (filename.rfind(".scs") != std::string::npos)
        {
          loader.loadScsFile(*context, componentDirName + "/" + filename);
        }
      }
      closedir(dir);
    }
  }
}
