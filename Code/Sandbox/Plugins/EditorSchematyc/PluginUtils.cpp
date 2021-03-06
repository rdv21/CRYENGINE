// Copyright 2001-2018 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"

#include "PluginUtils.h"

#include <IActionMapManager.h> // #SchematycTODO : Remove dependency on CryAction!
#include <IResourceSelectorHost.h>
#include <ISourceControl.h>
#include <CryEntitySystem/IEntityClass.h>
#include <CryEntitySystem/IEntitySystem.h>
#include <Cry3DEngine/I3DEngine.h>
#include <CryGame/IGameFramework.h>
#include <CryString/CryStringUtils.h>
#include <CrySchematyc/Utils/Assert.h>

#include <Controls/DictionaryWidget.h>
#include <Controls/QPopupWidget.h>

#include <QtUtil.h>
#include <QClipboard>
#include <QMimeData>
#include <QApplication>

namespace CrySchematycEditor {

enum EColumn : int32
{
	Column_Name,
	Column_COUNT
};

class CStringListDictionaryEntry : public CAbstractDictionaryEntry
{
	friend class CStringListDictionary;

public:
	CStringListDictionaryEntry() {}
	virtual ~CStringListDictionaryEntry() {}

	// CAbstractDictionaryEntry
	virtual uint32   GetType() const override { return CAbstractDictionaryEntry::Type_Entry; }

	virtual QVariant GetColumnValue(int32 columnIndex) const override
	{
		if (columnIndex == Column_Name)
		{
			return m_name;
		}
		return QVariant();
	}
	// ~CAbstractDictionaryEntry

	QString GetName() const { return m_name; }

private:
	QString m_name;
};

class CStringListDictionary : public CAbstractDictionary
{
public:
	CStringListDictionary(const Serialization::StringListStatic& names)
	{
		Load(names);
	}
	CStringListDictionary(const Serialization::StringList& names)
	{
		Load(names);
	}
	virtual ~CStringListDictionary() {}

	// CryGraphEditor::CAbstractDictionary
	virtual int32                           GetNumEntries() const override       { return m_names.size(); }
	virtual const CAbstractDictionaryEntry* GetEntry(int32 index) const override { return (m_names.size() > index) ? &m_names[index] : nullptr; }

	virtual int32                           GetNumColumns() const override       { return Column_COUNT; };
	virtual QString                         GetColumnName(int32 index) const override
	{
		if (index == Column_Name)
			return QString("Name");
		return QString();
	}

	virtual int32 GetDefaultFilterColumn() const override { return Column_Name; }
	// ~CryGraphEditor::CAbstractDictionary

	void Load(const Serialization::StringListStatic& names)
	{
		for (string name : names)
		{
			CStringListDictionaryEntry entry;
			entry.m_name = name;
			m_names.emplace_back(entry);
		}
	}

	void Load(const Serialization::StringList& names)
	{
		for (string name : names)
		{
			CStringListDictionaryEntry entry;
			entry.m_name = name;
			m_names.emplace_back(entry);
		}
	}

private:
	std::vector<CStringListDictionaryEntry> m_names;
};

class CActionMapActionQuickSearchOptions : public IActionMapPopulateCallBack
{
public:
	CActionMapActionQuickSearchOptions()
	{
		IActionMapManager* pActionMapManager = gEnv->pGameFramework->GetIActionMapManager();
		if (pActionMapManager && (pActionMapManager->GetActionsCount() > 0))
		{
			pActionMapManager->EnumerateActions(this);
		}
	}

	// IActionMapPopulateCallBack
	virtual void AddActionName(const char* const szName) override
	{
		m_names.push_back(szName);
	}
	// ~IActionMapPopulateCallBack

	Serialization::StringListStatic& GetNames() { return m_names; }

private:
	Serialization::StringListStatic m_names;
};

}

namespace {

//Helpers
void SearchFilesInFolder(string folderPath, Serialization::StringListStatic &names)
{
	//string vehiclesFolder = string(PathUtil::GetGameFolder()) + "/Scripts/Entities/Vehicles/Implementations/Xml/*.xml";
	string vehiclesFolder = folderPath;
	const char* pWildcardIn = vehiclesFolder.c_str();
	char cWorkBuf[255];
	gEnv->pCryPak->AdjustFileName(pWildcardIn, cWorkBuf, ICryPak::EPathResolutionRules::FLAGS_COPY_DEST_ALWAYS);

	char* cWork = cWorkBuf;

	_finddata_t fd;
	intptr_t h = gEnv->pCryPak->FindFirst(cWork, &fd, ICryPak::EPathResolutionRules::FLAGS_PATH_REAL, /*bAllOwUseFileSystem =*/ true);

	char cWildcardFullPath[MAX_PATH];
	cry_sprintf(cWildcardFullPath, "*.%s", PathUtil::GetExt(pWildcardIn));

	char* pDestName = strrchr(cWork, '\\');
	if (!pDestName)
		pDestName = cWork;
	else
		++pDestName;
	if (h != -1)
	{
		std::vector<string> files;
		do
		{
			strcpy(pDestName, fd.name);
			if (PathUtil::MatchWildcard(cWork, cWildcardFullPath))
			{
				string buffer(PathUtil::GetFileName(strlwr(cWork)));
				names.push_back(buffer);
			}
		} while (gEnv->pCryPak->FindNext(h, &fd) >= 0);
	}
}
////////////

dll_string EntityClassNameSelector(const SResourceSelectorContext& context, const char* szPreviousValue, Serialization::StringListValue* pStringListValue)
{
	Serialization::StringListStatic names;

	IEntityClassRegistry& entityClassRegistry = *gEnv->pEntitySystem->GetClassRegistry();
	entityClassRegistry.IteratorMoveFirst();

	while (IEntityClass* pClass = entityClassRegistry.IteratorNext())
	{
		names.push_back(pClass->GetName());
	}

	CrySchematycEditor::CStringListDictionary dict(names);
	CModalPopupDictionary dictionary("Entity Class", dict);

	const QPoint pos = QCursor::pos();
	dictionary.ExecAt(pos);

	CrySchematycEditor::CStringListDictionaryEntry* pEntry = static_cast<CrySchematycEditor::CStringListDictionaryEntry*>(dictionary.GetResult());
	if (pEntry)
	{
		return QtUtil::ToString(pEntry->GetName()).c_str();
	}

	return "";
}

dll_string ActionMapNameSelector(const SResourceSelectorContext& context, const char* szPreviousValue, Serialization::StringListValue* pStringListValue)
{
	Serialization::StringListStatic names;

	IActionMapIteratorPtr itActionMap = gEnv->pGameFramework->GetIActionMapManager()->CreateActionMapIterator();
	while (IActionMap* pActionMap = itActionMap->Next())
	{
		const char* szName = pActionMap->GetName();
		if (strcmp(szName, "default"))
		{
			names.push_back(szName);
		}
	}

	CrySchematycEditor::CStringListDictionary dict(names);
	CModalPopupDictionary dictionary("Action Map", dict);

	const QPoint pos = QCursor::pos();
	dictionary.ExecAt(pos);

	CrySchematycEditor::CStringListDictionaryEntry* pEntry = static_cast<CrySchematycEditor::CStringListDictionaryEntry*>(dictionary.GetResult());
	if (pEntry)
	{
		return QtUtil::ToString(pEntry->GetName()).c_str();
	}

	return "";
}

dll_string ActionMapActionNameSelector(const SResourceSelectorContext& context, const char* szPreviousValue, Serialization::StringListValue* pStringListValue)
{
	CrySchematycEditor::CActionMapActionQuickSearchOptions quickSearchOptions;

	CrySchematycEditor::CStringListDictionary dict(quickSearchOptions.GetNames());
	CModalPopupDictionary dictionary("Action Map Action", dict);

	const QPoint pos = QCursor::pos();
	dictionary.ExecAt(pos);

	CrySchematycEditor::CStringListDictionaryEntry* pEntry = static_cast<CrySchematycEditor::CStringListDictionaryEntry*>(dictionary.GetResult());
	if (pEntry)
	{
		return QtUtil::ToString(pEntry->GetName()).c_str();
	}

	return "";
}

dll_string SurfaceTypeNameSelector(const SResourceSelectorContext& context, const char* szPreviousValue, Serialization::StringListValue* pStringListValue)
{
	Serialization::StringListStatic names;

	ISurfaceTypeEnumerator* pEnumerator = gEnv->p3DEngine->GetMaterialManager()->GetSurfaceTypeManager()->GetEnumerator();
	for (ISurfaceType* pSurfaceType = pEnumerator->GetFirst(); pSurfaceType != nullptr; pSurfaceType = pEnumerator->GetNext())
	{
		cstr name = pSurfaceType->GetName();
		if (strlen(name) >= 4 && !strncmp(name, "mat_", 4))
			name += 4;
		names.push_back(name);
	}

	CrySchematycEditor::CStringListDictionary dict(names);
	CModalPopupDictionary dictionary("Surface Type", dict);

	const QPoint pos = QCursor::pos();
	dictionary.ExecAt(pos);

	CrySchematycEditor::CStringListDictionaryEntry* pEntry = static_cast<CrySchematycEditor::CStringListDictionaryEntry*>(dictionary.GetResult());
	if (pEntry)
	{
		return QtUtil::ToString(pEntry->GetName()).c_str();
	}

	return "";
}

static dll_string MannequinScopeContextName(const SResourceSelectorContext& context, const char* szPreviousValue, Serialization::StringListValue* pStringListValue)
{
	if (std::shared_ptr<Serialization::SMannequinControllerDefResourceParams> pParams = std::static_pointer_cast<Serialization::SMannequinControllerDefResourceParams>(context.pCustomParams))
	{
		if (pParams->pControllerDef != nullptr)
		{
			Serialization::StringListStatic names;

			for (int i = 0, n = pParams->pControllerDef->m_scopeContexts.GetNum(); i < n; ++i)
			{
				names.push_back(pParams->pControllerDef->m_scopeContexts.GetTagName(i));
			}

			CrySchematycEditor::CStringListDictionary dict(names);
			CModalPopupDictionary dictionary("Mannequin Scope Context", dict);

			const QPoint pos = QCursor::pos();
			dictionary.ExecAt(pos);

			CrySchematycEditor::CStringListDictionaryEntry* pEntry = static_cast<CrySchematycEditor::CStringListDictionaryEntry*>(dictionary.GetResult());
			if (pEntry)
			{
				return QtUtil::ToString(pEntry->GetName()).c_str();
			}
		}
	}

	return "";
}

static dll_string MannequinFragmentName(const SResourceSelectorContext& context, const char* szPreviousValue, Serialization::StringListValue* pStringListValue)
{
	if (std::shared_ptr<Serialization::SMannequinControllerDefResourceParams> pParams = std::static_pointer_cast<Serialization::SMannequinControllerDefResourceParams>(context.pCustomParams))
	{
		if (pParams->pControllerDef != nullptr)
		{
			Serialization::StringListStatic names;

			for (int i = 0, n = pParams->pControllerDef->m_fragmentIDs.GetNum(); i < n; ++i)
			{
				names.push_back(pParams->pControllerDef->m_fragmentIDs.GetTagName(i));
			}

			CrySchematycEditor::CStringListDictionary dict(names);
			CModalPopupDictionary dictionary("Mannequin Fragment", dict);

			const QPoint pos = QCursor::pos();
			dictionary.ExecAt(pos);

			CrySchematycEditor::CStringListDictionaryEntry* pEntry = static_cast<CrySchematycEditor::CStringListDictionaryEntry*>(dictionary.GetResult());
			if (pEntry)
			{
				return QtUtil::ToString(pEntry->GetName()).c_str();
			}
		}
	}

	return "";
}

dll_string VehicleFileNameSelector(const SResourceSelectorContext& context, const char* szPreviousValue, Serialization::StringListValue* pStringListValue)
{
	//list all files from Scripts/Entities/Vehicles/Implementations/Xml
	Serialization::StringListStatic names;  

	string vehiclesFolder = string(PathUtil::GetGameFolder()) + "/Scripts/Entities/Vehicles/Implementations/Xml/*.xml";
	SearchFilesInFolder(vehiclesFolder,names);

	CrySchematycEditor::CStringListDictionary dict(names);
	CModalPopupDictionary dictionary("Vehicle", dict);

	const QPoint pos = QCursor::pos();
	dictionary.ExecAt(pos);

	CrySchematycEditor::CStringListDictionaryEntry* pEntry = static_cast<CrySchematycEditor::CStringListDictionaryEntry*>(dictionary.GetResult());
	if (pEntry)
	{
		return QtUtil::ToString(pEntry->GetName()).c_str();
	}

	return "";
}

dll_string VehicleModificationFileNameSelector(const SResourceSelectorContext& context, const char* szPreviousValue, Serialization::StringListValue* pStringListValue)
{
	Serialization::StringListStatic names;

	string vehiclesFolder = string(PathUtil::GetGameFolder()) + "/Scripts/Entities/Vehicles/Implementations/Xml/Modifications/*.xml";
	SearchFilesInFolder(vehiclesFolder, names);

	CrySchematycEditor::CStringListDictionary dict(names);
	CModalPopupDictionary dictionary("VehicleModification", dict);

	const QPoint pos = QCursor::pos();
	dictionary.ExecAt(pos);

	CrySchematycEditor::CStringListDictionaryEntry* pEntry = static_cast<CrySchematycEditor::CStringListDictionaryEntry*>(dictionary.GetResult());
	if (pEntry)
	{
		return QtUtil::ToString(pEntry->GetName()).c_str();
	}

	return "";
}

dll_string VehicleLoadoutFileNameSelector(const SResourceSelectorContext& context, const char* szPreviousValue, Serialization::StringListValue* pStringListValue)
{
	Serialization::StringListStatic names;

	string vehiclesFolder = string(PathUtil::GetGameFolder()) + "/Scripts/Loadouts/Vehicles/*.xml";
	SearchFilesInFolder(vehiclesFolder, names);

	CrySchematycEditor::CStringListDictionary dict(names);
	CModalPopupDictionary dictionary("VehicleLoadout", dict);

	const QPoint pos = QCursor::pos();
	dictionary.ExecAt(pos);

	CrySchematycEditor::CStringListDictionaryEntry* pEntry = static_cast<CrySchematycEditor::CStringListDictionaryEntry*>(dictionary.GetResult());
	if (pEntry)
	{
		return QtUtil::ToString(pEntry->GetName()).c_str();
	}

	return "";
}

dll_string PlayerLoadoutFileNameSelector(const SResourceSelectorContext& context, const char* szPreviousValue, Serialization::StringListValue* pStringListValue)
{
	return "";
}

dll_string CharacterLoadoutFileNameSelector(const SResourceSelectorContext& context, const char* szPreviousValue, Serialization::StringListValue* pStringListValue)
{
	return "";
}

dll_string PlayerCharacterLoadoutNameSelector(const SResourceSelectorContext& context, const char* szPreviousValue, Serialization::StringListValue* pStringListValue)
{
	//search in both player and character folders
	return "";
}

REGISTER_RESOURCE_SELECTOR("EntityClass", EntityClassNameSelector, "")
REGISTER_RESOURCE_SELECTOR("ActionMap", ActionMapNameSelector, "")
REGISTER_RESOURCE_SELECTOR("ActionMapAction", ActionMapActionNameSelector, "")
REGISTER_RESOURCE_SELECTOR("SurfaceTypeName", SurfaceTypeNameSelector, "")
REGISTER_RESOURCE_SELECTOR("MannequinScopeContextName", MannequinScopeContextName, "")
REGISTER_RESOURCE_SELECTOR("MannequinFragmentName", MannequinFragmentName, "")
REGISTER_RESOURCE_SELECTOR("VehicleName", VehicleFileNameSelector, "")
REGISTER_RESOURCE_SELECTOR("VehicleModificationName", VehicleModificationFileNameSelector, "")
REGISTER_RESOURCE_SELECTOR("VehicleLoadoutName", VehicleLoadoutFileNameSelector, "")
REGISTER_RESOURCE_SELECTOR("PlayerLoadoutName", PlayerLoadoutFileNameSelector, "")
REGISTER_RESOURCE_SELECTOR("CharacterLoadoutName", CharacterLoadoutFileNameSelector, "")
REGISTER_RESOURCE_SELECTOR("PlayerCharacterLoadoutName", PlayerCharacterLoadoutNameSelector, "")
}

namespace CrySchematycEditor {
namespace Utils {

void ConstructAbsolutePath(Schematyc::IString& output, const char* szFileName)
{
	char currentDirectory[512] = "";
	GetCurrentDirectory(sizeof(currentDirectory) - 1, currentDirectory);

	Schematyc::CStackString temp = currentDirectory;
	temp.append("\\");
	temp.append(szFileName);
	temp.replace("/", "\\");

	output.assign(temp);
}

bool WriteToClipboard(const char* szText, const char* szPrefix)
{
	SCHEMATYC_EDITOR_ASSERT(szText);
	if (szText)
	{
		QMimeData* pMimeData = new QMimeData();
		if (szPrefix)
			pMimeData->setData(szPrefix, szText);
		else
			pMimeData->setText(szText);

		QClipboard* pClipboard = QApplication::clipboard();
		pClipboard->setMimeData(pMimeData);

		return true;
	}
	return false;
}

bool ReadFromClipboard(string& text, const char* szPrefix)
{
	QClipboard* pClipboard = QApplication::clipboard();
	if (pClipboard)
	{
		const QMimeData* pMimeData = pClipboard->mimeData();
		if (szPrefix)
		{
			QByteArray byteArray = pMimeData->data(szPrefix);
			if (byteArray.length() > 0)
			{
				text.reserve(byteArray.length());
				text.assign(byteArray.data(), byteArray.length());
				return true;
			}
		}
		else
		{
			text = pMimeData->text().toUtf8();
			return text.length() > 0;
		}
	}

	text.clear();
	return false;
}

bool ValidateClipboardContents(const char* szPrefix)
{
	SCHEMATYC_EDITOR_ASSERT(szPrefix);

	QClipboard* pClipboard = QApplication::clipboard();
	if (pClipboard)
	{
		const QMimeData* pMimeData = pClipboard->mimeData();
		QByteArray byteArray = pMimeData->data(szPrefix);
		if (byteArray.length() > 0)
		{
			return true;
		}
	}
	return false;
}

}
}

