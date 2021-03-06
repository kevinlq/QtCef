#include "stable.h"
#include "qcefapiobject.h"
#include "qcefapiadapter.h"
#include "qthack/genericmetacall.h"
#include "qthack/genericsignalmap.h"

QCefApiAdapter::QCefApiAdapter(QCefOSWidget* w, QObject* parent)
    : QObject(parent)
    , m_cefWidget(w)
{
}

void QCefApiAdapter::initApi(QCefApiObject* apiImpl, QString parentPath, QString apiName)
{
    apiImpl->applyApiAdapter(this);

    m_apiObject = apiImpl;
    m_apiName = apiName;
    m_parentPath = parentPath;

    connect(m_cefWidget, SIGNAL(jsInvokeMsg(const QString&, const QString&, const QVariantList&)), SLOT(onJsInvokeMsg(const QString&, const QString&, const QVariantList&)));
    connect(m_cefWidget, SIGNAL(addEventListnerMsg(const QString&)), SLOT(onAddEventListnerMsg(const QString&)));

    parseSignal();
}

QString QCefApiAdapter::apiPath()
{
    if (m_parentPath.isEmpty())
    {
        return m_apiName;
    }
    else
    {
        return m_parentPath + "." + m_apiName;
    }
}

void QCefApiAdapter::runJavascript(const QString& code)
{
    if (m_cefWidget)
    {
        m_cefWidget->runJavascript(code);
    }
}

void QCefApiAdapter::invokeEvent(QString eventName, QVariantList arguments)
{
    if (m_cefWidget)
    {
        QString eventPath = apiPath();
        eventPath.append(".").append(eventName);

        CefRefPtr<CefListValue> cefList = JsFunctionWrapper::convertToCefList(arguments);
        CefRefPtr<CefProcessMessage> processMessage = CefProcessMessage::Create("InvokeEvent");
        processMessage->GetArgumentList()->SetString(0, eventPath.toStdWString());
        processMessage->GetArgumentList()->SetList(1, cefList);
        m_cefWidget->sendProcessMessage(PID_RENDERER, processMessage);
    }
}

struct MetaCallArg
{
    QObject* obj;
    QMetaMethod metaMethod;
    const QVariantList& args;
};

void metaCall_(void* a)
{
    auto arg = (MetaCallArg*)a;
    Qt::metaCall(arg->obj, arg->metaMethod, arg->args);
}

bool metaCallSEH_(void* a)
{
    __try {
        metaCall_(a);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    return true;
}

void QCefApiAdapter::onJsInvokeMsg(const QString& object, const QString& method, const QVariantList& args)
{
    if (object != apiPath())
    {
        return;
    }

    const QMetaObject* metaObject = m_apiObject->metaObject();

    for (int i = 0; i != metaObject->methodCount(); ++i)
    {
        QMetaMethod metaMethod = metaObject->method(i);
        if (metaMethod.methodType() == QMetaMethod::Method 
            || metaMethod.methodType() == QMetaMethod::Slot)
        {
            QString methodName = QLatin1String(metaMethod.methodSignature());
            methodName = methodName.left(methodName.indexOf("("));

            if (methodName == method)
            {
                auto paramTypes = metaMethod.parameterTypes();
                QVariantList args2 = args;
                for (int index = 0; index != args2.size(); ++index)
                {
                    int paramType = QMetaType::type(paramTypes[index].constData());

                    if (args2[index].userType() == QMetaType::Int)
                    {
                        if (paramType == QMetaType::QReal)
                        {
                            args2[index] = QVariant::fromValue((qreal)args2[index].toInt());
                        }
                    }
                    else if (args2[index].userType() == QMetaType::QReal)
                    {
                        if (paramType == QMetaType::Int)
                        {
                            args2[index] = QVariant::fromValue((int)args2[index].toReal());
                        }
                    }
                }

                metaCallSEH_((void*)&MetaCallArg { m_apiObject, metaMethod, args2 });
                break;
            }
        }
    }
}

void QCefApiAdapter::onAddEventListnerMsg(const QString& path)
{
    m_apiObject->metaObject()->invokeMethod(m_apiObject, "onAddEventListnerMsg", Q_ARG(QString, path));
}

void QCefApiAdapter::onGenericSignal(QObject* sender, QMetaMethod signal, QVariantList args)
{
    (void)sender;

    QString methodName = QLatin1String(signal.methodSignature());

    methodName = methodName.left(methodName.indexOf("("));

    invokeEvent(methodName, args);
}

void QCefApiAdapter::parseSignal()
{
    const QMetaObject* metaObject = m_apiObject->metaObject();

    for (int i = 0; i != metaObject->methodCount(); ++i)
    {
        QMetaMethod method = metaObject->method(i);
        if (method.methodType() == QMetaMethod::Signal)
        {
            QString signature = QLatin1String(method.methodSignature());
            QString eventName = signature.left(signature.indexOf("("));
            if (eventName == "destroyed")
            {
                continue;
            }

            signature.prepend("2");

            GenericSignalMapper* mapper1 = new GenericSignalMapper(method, this);
            connect(m_apiObject, qFlagLocation(signature.toUtf8().constData()), mapper1, SLOT(mapSlot()));
            connect(mapper1, SIGNAL(mapped(QObject*, QMetaMethod, QVariantList)), SLOT(onGenericSignal(QObject*, QMetaMethod, QVariantList)));

            m_mappers.append(mapper1);
        }
    }
}
